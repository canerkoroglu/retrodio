/*
 * T5: MicroSD mount, card info, greeting file check, and a full-length write benchmark that
 * reproduces the recorder's access pattern (4 KB writes, header rewrite + fsync every 5 s of audio).
 * The latency histogram sizes the RAM ring buffer used during recording.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "hwtest.h"
#include "esp_timer.h"
#include "rg_board.h"
#include "rg_storage.h"
#include "rg_wav.h"

#define BENCH_PATH  RG_DIR_RECORDINGS "/BENCH.TMP"
#define CHUNK       4096
#define TOTAL_BYTES (180 * RG_BYTES_PER_SECOND) /* one maximum-length message */
#define SYNC_EVERY  (5 * RG_BYTES_PER_SECOND / CHUNK)

static uint32_t s_buf[CHUNK / 4];
static const uint32_t BUCKET_MS[] = {2, 5, 10, 20, 50, 100, 200, 500, 1000};
#define NBUCKETS (sizeof(BUCKET_MS) / sizeof(BUCKET_MS[0]) + 1)

static void bucket_add(uint32_t *hist, uint32_t us)
{
    size_t i = 0;
    while (i < NBUCKETS - 1 && us >= BUCKET_MS[i] * 1000) {
        i++;
    }
    hist[i]++;
}

static void check_greeting(const char *path)
{
    rg_wav_info_t info;
    esp_err_t err = rg_wav_probe(path, &info);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("  %-40s missing\n", path);
        return;
    }
    if (err != ESP_OK) {
        printf("  %-40s not a valid WAV\n", path);
        return;
    }
    printf("  %-40s %lu Hz, %u ch, %u bit, fmt %u, %.1f s -> %s\n", path, (unsigned long)info.sample_rate,
           info.channels, info.bits, info.format,
           info.sample_rate ? (double)info.data_bytes / (info.sample_rate * info.channels * info.bits / 8) : 0.0,
           rg_wav_is_playable(&info) ? "OK" : "WRONG FORMAT (need 16000 Hz mono 16-bit PCM)");
}

tr_t t5_sd(void)
{
    uint32_t khz = hw_sd_mount();
    if (!khz) {
        hw_result(5, TR_FAIL, "mount=failed hint=check_5V_power_wiring_card_FAT32");
        return TR_FAIL;
    }
    sdmmc_card_print_info(stdout, rg_storage_card());
    uint64_t total = 0, free_b = 0;
    rg_storage_space(&total, &free_b);
    printf("Mounted at %lu kHz. FAT: %llu MB total, %llu MB free -> room for ~%llu max-length messages\n",
           (unsigned long)khz, total >> 20, free_b >> 20, free_b / TOTAL_BYTES);

    printf("Greeting files:\n");
    check_greeting(RG_FILE_GREETING);
    check_greeting(RG_FILE_INSTRUCTION);
    check_greeting(RG_FILE_BEEP);
    hw_print_card_services();

    if (free_b < TOTAL_BYTES * 2ULL) {
        hw_result(5, TR_FAIL, "mount_khz=%lu free_mb=%llu error=card_too_full_for_benchmark", (unsigned long)khz,
                  free_b >> 20);
        return TR_FAIL;
    }

    printf("Writing %u bytes (one 180 s message) in %u B chunks...\n", TOTAL_BYTES, CHUNK);
    unlink(BENCH_PATH);
    int fd = open(BENCH_PATH, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        hw_result(5, TR_FAIL, "mount_khz=%lu error=create_failed", (unsigned long)khz);
        return TR_FAIL;
    }
    uint32_t hist[NBUCKETS] = {0};
    uint32_t max_write_us = 0, max_sync_us = 0, word = 0, chunks = 0;
    bool io_ok = true;
    int64_t start = esp_timer_get_time();
    for (uint32_t done = 0; done < TOTAL_BYTES; done += CHUNK, chunks++) {
        for (size_t i = 0; i < CHUNK / 4; i++) {
            s_buf[i] = word++;
        }
        int64_t t0 = esp_timer_get_time();
        if (write(fd, s_buf, CHUNK) != CHUNK) {
            io_ok = false;
            break;
        }
        uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
        if (chunks % SYNC_EVERY == SYNC_EVERY - 1) {
            /* Recorder checkpoint: rewrite the 44-byte header, then fsync. */
            int64_t t1 = esp_timer_get_time();
            uint8_t hdr[RG_WAV_HEADER_SIZE];
            rg_wav_build_header(hdr, RG_SAMPLE_RATE_HZ, 1, 16, done + CHUNK - RG_WAV_HEADER_SIZE);
            if (pwrite(fd, hdr, sizeof(hdr), 0) != sizeof(hdr) || fsync(fd) != 0) {
                io_ok = false;
                break;
            }
            uint32_t ds = (uint32_t)(esp_timer_get_time() - t1);
            max_sync_us = ds > max_sync_us ? ds : max_sync_us;
            dt += ds;
        }
        max_write_us = dt > max_write_us ? dt : max_write_us;
        bucket_add(hist, dt);
    }
    bool closed_ok = !io_ok || fsync(fd) == 0;
    closed_ok = close(fd) == 0 && closed_ok;
    io_ok = io_ok && closed_ok;
    uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start) / 1000);
    uint32_t write_kbps = elapsed_ms ? (uint32_t)((uint64_t)TOTAL_BYTES * 1000 / elapsed_ms / 1024) : 0;

    printf("Verifying read-back...\n");
    bool verify_ok = io_ok;
    uint32_t read_kbps = 0;
    if (io_ok) {
        fd = open(BENCH_PATH, O_RDONLY);
        word = 0;
        start = esp_timer_get_time();
        for (uint32_t done = 0; fd >= 0 && done < TOTAL_BYTES && verify_ok; done += CHUNK) {
            if (read(fd, s_buf, CHUNK) != CHUNK) {
                verify_ok = false;
                break;
            }
            for (size_t i = 0; i < CHUNK / 4; i++, word++) {
                /* The first 44 bytes hold the checkpointed WAV header, not the counter pattern. */
                if (word >= RG_WAV_HEADER_SIZE / 4 && s_buf[i] != word) {
                    printf("Mismatch at byte %lu\n", (unsigned long)(done + i * 4));
                    verify_ok = false;
                    break;
                }
            }
        }
        uint32_t rms = (uint32_t)((esp_timer_get_time() - start) / 1000);
        read_kbps = rms ? (uint32_t)((uint64_t)TOTAL_BYTES * 1000 / rms / 1024) : 0;
        verify_ok = verify_ok && fd >= 0;
        if (fd >= 0) {
            close(fd);
        }
    }
    unlink(BENCH_PATH);

    printf("\nPer-chunk latency (write + periodic checkpoint):\n");
    for (size_t i = 0; i < NBUCKETS; i++) {
        if (i < NBUCKETS - 1) {
            printf("  < %4lu ms : %lu\n", (unsigned long)BUCKET_MS[i], (unsigned long)hist[i]);
        } else {
            printf("  >=%4lu ms : %lu\n", (unsigned long)BUCKET_MS[i - 1], (unsigned long)hist[i]);
        }
    }
    /* Ring buffer must cover the worst stall twice over; round up to 16 KB, minimum 32 KB. */
    uint32_t ring = (uint32_t)((uint64_t)max_write_us * RG_BYTES_PER_SECOND / 1000000 * 2);
    ring = (ring + 16383) & ~16383U;
    ring = ring < 32768 ? 32768 : ring;
    uint32_t realtime_x = write_kbps * 1024 / RG_BYTES_PER_SECOND;

    tr_t r = io_ok && verify_ok && realtime_x >= 4 && ring <= 98304 ? TR_PASS : TR_FAIL;
    hw_result(5, r,
              "mount_khz=%lu io=%s verify=%s write_kBps=%lu read_kBps=%lu realtime_x=%lu max_write_ms=%lu "
              "max_checkpoint_ms=%lu ring_kb=%lu free_mb=%llu",
              (unsigned long)khz, io_ok ? "ok" : "error", verify_ok ? "ok" : "error", (unsigned long)write_kbps,
              (unsigned long)read_kbps, (unsigned long)realtime_x, (unsigned long)(max_write_us / 1000),
              (unsigned long)(max_sync_us / 1000), (unsigned long)(ring / 1024), free_b >> 20);
    if (khz < CONFIG_RG_SD_FREQ_KHZ) {
        printf("NOTE: mount only worked at %lu kHz. If the module has resistor dividers instead of a buffer IC,\n"
               "      bypass them (the ESP32 is already 3.3 V logic) and retest.\n",
               (unsigned long)khz);
    }
    return r;
}
