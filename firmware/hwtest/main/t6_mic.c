/*
 * T6: I2S MEMS microphone.
 *  a) start-up time (zero output after clock start)
 *  b) slot detection (L/R wiring) and real bit depth (identifies 24-bit vs other MEMS parts)
 *  c) 10 s live level meter
 *  d) 10 s WAV recording through the production converter + recorder, then optional playback
 */
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include "hwtest.h"
#include "esp_timer.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_err.h"
#include "esp_system.h"
#include "rg_recorder.h"
#include "rg_storage.h"

#define FRAMES   RG_AUDIO_DMA_FRAMES
#define TEST_WAV RG_DIR_RECORDINGS "/TEST_MIC.wav"

static int32_t s_raw[FRAMES * 2];
static int16_t s_pcm[FRAMES];

typedef struct {
    uint32_t nonzero;
    uint32_t or_bits;
    int64_t sum;
    double sumsq;
} slot_stats_t;

static double dbfs(double v)
{
    return v > 0 ? 20.0 * log10(v / 8388608.0) : -140.0;
}

static bool read_frames(size_t *frames)
{
    size_t got = 0;
    if (rg_audio_read(s_raw, FRAMES * 2, &got, 500) != ESP_OK) {
        return false;
    }
    *frames = got / 2;
    return true;
}

tr_t t6_mic(void)
{
    esp_err_t err = rg_audio_open(RG_AUDIO_RX, true);
    if (err != ESP_OK) {
        hw_result(6, TR_FAIL, "i2s_open=%s", esp_err_to_name(err));
        return TR_FAIL;
    }
    hw_oled_status("MIC TEST", "STARTING");

    /* a) start-up */
    int32_t startup_ms = -1;
    uint32_t frames_seen = 0;
    while (frames_seen < RG_SAMPLE_RATE_HZ) {
        size_t n;
        if (!read_frames(&n)) {
            break;
        }
        for (size_t i = 0; i < n * 2 && startup_ms < 0; i++) {
            if (s_raw[i] != 0) {
                startup_ms = (int32_t)((frames_seen + i / 2) * 1000 / RG_SAMPLE_RATE_HZ);
            }
        }
        frames_seen += n;
        if (startup_ms >= 0) {
            break;
        }
    }
    printf("First non-zero sample after %ld ms (INMP441 datasheet: 2^18 SCK = 256 ms at 1.024 MHz)\n",
           (long)startup_ms);
    if (startup_ms < 0) {
        rg_audio_close();
        hw_result(6, TR_FAIL, "data=all_zero hint=check_SD_to_GPIO%d_VDD_3V3_SCK_WS", RG_PIN_I2S_DIN);
        return TR_FAIL;
    }

    /* b) one second of statistics per slot */
    slot_stats_t st[2] = {0};
    for (uint32_t total = 0; total < RG_SAMPLE_RATE_HZ;) {
        size_t n;
        if (!read_frames(&n)) {
            break;
        }
        for (size_t i = 0; i < n; i++) {
            for (int s = 0; s < 2; s++) {
                int32_t w = s_raw[i * 2 + s];
                int32_t v = w >> 8;
                st[s].nonzero += w != 0;
                st[s].or_bits |= (uint32_t)w;
                st[s].sum += v;
                st[s].sumsq += (double)v * v;
            }
        }
        total += n;
    }
    int active = st[1].nonzero > st[0].nonzero ? 1 : 0;
    const char *slot_name[] = {"LEFT", "RIGHT"};
    int bits[2];
    for (int s = 0; s < 2; s++) {
        bits[s] = st[s].or_bits ? 32 - __builtin_ctz(st[s].or_bits) : 0;
        double mean = (double)st[s].sum / RG_SAMPLE_RATE_HZ;
        double rms = sqrt(st[s].sumsq / RG_SAMPLE_RATE_HZ - mean * mean);
        printf("  %-5s slot: nonzero=%5lu valid_bits=%2d dc=%.0f noise_rms=%.1f dBFS\n", slot_name[s],
               (unsigned long)st[s].nonzero, bits[s], mean, dbfs(rms));
    }
    printf("Active slot: %s (expected LEFT with L/R tied to GND). Valid bits: %d (INMP441 = 24).\n", slot_name[active],
           bits[active]);

    /* c) level meter */
    printf("\nSpeak into the handset for 10 s (peak / RMS in dBFS):\n");
    hw_oled_status("MIC TEST", "SPEAK NOW");
    double loudest_peak = -140;
    for (int block = 0; block < 40; block++) {
        double sumsq = 0;
        int32_t peak = 0;
        uint32_t count = 0;
        while (count < RG_SAMPLE_RATE_HZ / 4) {
            size_t n;
            if (!read_frames(&n)) {
                break;
            }
            for (size_t i = 0; i < n; i++) {
                int32_t v = s_raw[i * 2 + active] >> 8;
                int32_t a = v < 0 ? -v : v;
                peak = a > peak ? a : peak;
                sumsq += (double)v * v;
            }
            count += n;
        }
        double p = dbfs(peak), r = dbfs(sqrt(sumsq / (count ? count : 1)));
        loudest_peak = p > loudest_peak ? p : loudest_peak;
        int bar = (int)((r + 90) / 2);
        bar = bar < 0 ? 0 : (bar > 40 ? 40 : bar);
        printf("  peak %6.1f  rms %6.1f  |%.*s\n", p, r, bar, "########################################");
    }
    uint32_t ovf = rg_audio_rx_overflows();

    /* d) recording */
    rg_rec_stats_t rs = {0};
    esp_err_t rec_err = ESP_ERR_NOT_FOUND;
    if (hw_sd_mount()) {
        unlink(TEST_WAV);
        rec_err = rg_rec_start(TEST_WAV, 64 * 1024, 1024 * 1024);
        if (rec_err == ESP_OK) {
            printf("\nRecording 10 s to %s (gain %d dB)...\n", TEST_WAV, g_settings.mic_gain_db);
            hw_oled_status("MIC TEST", "RECORDING 10 S");
            rg_mic_conv_t conv;
            rg_mic_conv_init(&conv, g_settings.mic_gain_db, CONFIG_RG_MIC_DC_BLOCK);
            rg_audio_rx_flush();
            for (uint32_t total = 0; total < 10 * RG_SAMPLE_RATE_HZ && !rg_rec_failed();) {
                size_t n;
                if (!read_frames(&n)) {
                    break;
                }
                rg_mic_conv_run(&conv, s_raw + active, 2, s_pcm, n);
                rg_rec_push(s_pcm, n * sizeof(int16_t));
                total += n;
            }
            rec_err = rg_rec_stop(&rs, 5000);
            if (rec_err == RG_ERR_TIMEOUT) {
                printf("SD writer stuck: rebooting (the file is repaired on the next mount)\n");
                esp_restart();
            }
            printf("Recorder: %s, %lu B, crc32 %08lx, dropped %lu B, clipped %lu, max write %lu ms, ring peak %lu B\n",
                   rg_err_name(rec_err), (unsigned long)rs.data_bytes, (unsigned long)rs.crc32,
                   (unsigned long)rs.dropped_bytes, (unsigned long)conv.clipped,
                   (unsigned long)(rs.max_write_us / 1000), (unsigned long)rs.ring_high_water);
        }
    } else {
        printf("SD not mounted: skipping the recording step.\n");
    }
    rg_audio_close();

    if (rec_err == ESP_OK && hw_ask_yes("Play the recording back through the earpiece?")) {
        if (rg_audio_open(RG_AUDIO_TX, false) == ESP_OK) {
            rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 50);
            rg_audio_play_wav(TEST_WAV, NULL, NULL);
            rg_audio_drain();
            rg_audio_close();
        }
    }

    bool slot_ok = active == 0;
    bool bits_ok = bits[active] >= 18;
    tr_t r = slot_ok && bits_ok && ovf == 0 && (rec_err == ESP_OK || rec_err == ESP_ERR_NOT_FOUND) ? TR_PASS : TR_FAIL;
    hw_result(6, r,
              "startup_ms=%ld slot=%s valid_bits=%d noise_dbfs=%.1f loudest_peak_dbfs=%.1f rx_overflows=%lu "
              "record=%s dropped=%lu gain_db=%d",
              (long)startup_ms, slot_name[active], bits[active], dbfs(sqrt(st[active].sumsq / RG_SAMPLE_RATE_HZ)),
              loudest_peak, (unsigned long)ovf, rg_err_name(rec_err), (unsigned long)rs.dropped_bytes,
              g_settings.mic_gain_db);
    return r;
}
