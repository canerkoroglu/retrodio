#include "rg_recorder.h"
#include <string.h>
#include <unistd.h>
#include "rg_board.h"
#include "rg_crc32.h"
#include "rg_err.h"
#include "rg_storage.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#define WRITE_CHUNK      4096                      /* multiple of the 512 B sector */
#define CHECKPOINT_BYTES (5 * RG_BYTES_PER_SECOND) /* header + fsync every 5 s of audio */
#define WRITER_PRIO      5
#define WRITER_STACK     4096
#define NVS_NS           "rg_rec"
#define NVS_KEY          "inprog"

static const char *TAG = "rg_rec";

static struct {
    StreamBufferHandle_t ring;
    TaskHandle_t task;
    TaskHandle_t waiter;
    rg_wav_writer_t wav;
    char path[96];
    size_t ring_bytes;
    volatile bool stop;
    volatile bool failed;
    esp_err_t result;
    uint32_t good_bytes; /* bytes confirmed written; the file is truncated to this on failure */
    bool close_ok;
    rg_rec_stats_t stats;
} s;

static uint8_t s_chunk[WRITE_CHUNK];

static esp_err_t marker_set(const char *path)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = path ? nvs_set_str(h, NVS_KEY, path) : nvs_erase_key(h, NVS_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

static void track_ring_fill(void)
{
    size_t used = s.ring_bytes - xStreamBufferSpacesAvailable(s.ring);
    if (used > s.stats.ring_high_water) {
        s.stats.ring_high_water = (uint32_t)used;
    }
}

static void write_chunk(size_t len, uint32_t *since_checkpoint)
{
    if (s.failed || len == 0) {
        return; /* keep draining so the producer never blocks */
    }
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = rg_wav_writer_write(&s.wav, s_chunk, len);
    uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
    if (dt > s.stats.max_write_us) {
        s.stats.max_write_us = dt;
    }
    if (err == ESP_OK) {
        s.stats.crc32 = rg_crc32_update(s.stats.crc32, s_chunk, len);
        s.good_bytes += (uint32_t)len;
        *since_checkpoint += len;
        if (*since_checkpoint >= CHECKPOINT_BYTES) {
            t0 = esp_timer_get_time();
            err = rg_wav_writer_checkpoint(&s.wav);
            dt = (uint32_t)(esp_timer_get_time() - t0);
            if (dt > s.stats.max_checkpoint_us) {
                s.stats.max_checkpoint_us = dt;
            }
            *since_checkpoint = 0;
        }
    }
    if (err != ESP_OK) {
        s.failed = true;
        s.result = err;
    }
}

static void writer_task(void *arg)
{
    (void)arg;
    /* A write that blocks past CONFIG_ESP_TASK_WDT_TIMEOUT_S panics -> reboot -> rg_rec_recover(). */
    esp_task_wdt_add(NULL);
    size_t fill = 0;
    uint32_t since_checkpoint = 0;
    for (;;) {
        esp_task_wdt_reset();
        track_ring_fill();
        size_t n = xStreamBufferReceive(s.ring, s_chunk + fill, WRITE_CHUNK - fill, pdMS_TO_TICKS(50));
        fill += n;
        if (fill == WRITE_CHUNK) {
            write_chunk(fill, &since_checkpoint);
            fill = 0;
        }
        if (s.stop && n == 0 && xStreamBufferIsEmpty(s.ring)) {
            write_chunk(fill, &since_checkpoint);
            break;
        }
    }
    if (s.failed && s.wav.fd >= 0) {
        /* Drop a partially written chunk so the header, the data and the CRC all describe exactly
         * the bytes that reached the card (e.g. the card filled up mid-message). */
        ftruncate(s.wav.fd, (off_t)(RG_WAV_HEADER_SIZE + s.good_bytes));
        s.wav.data_bytes = s.good_bytes;
    }
    esp_err_t err = rg_wav_writer_close(&s.wav);
    if (s.result == ESP_OK) {
        s.result = err;
    }
    s.close_ok = err == ESP_OK;
    s.stats.data_bytes = s.wav.data_bytes;
    esp_task_wdt_delete(NULL);
    xTaskNotifyGive(s.waiter);
    vTaskDelete(NULL);
}

esp_err_t rg_rec_start(const char *path, size_t ring_bytes, uint64_t min_free_bytes)
{
    ESP_RETURN_ON_FALSE(!s.task, ESP_ERR_INVALID_STATE, TAG, "busy");
    uint64_t total, free_bytes;
    ESP_RETURN_ON_ERROR(rg_storage_space(&total, &free_bytes), TAG, "space");
    ESP_RETURN_ON_FALSE(free_bytes >= min_free_bytes, RG_ERR_CARD_FULL, TAG, "card full (%llu B free)",
                        (unsigned long long)free_bytes);

    memset(&s.stats, 0, sizeof(s.stats));
    s.stop = false;
    s.failed = false;
    s.result = ESP_OK;
    s.good_bytes = 0;
    s.close_ok = false;
    s.ring_bytes = ring_bytes;
    strlcpy(s.path, path, sizeof(s.path));
    s.ring = xStreamBufferCreate(ring_bytes, 1);
    ESP_RETURN_ON_FALSE(s.ring, ESP_ERR_NO_MEM, TAG, "ring");

    esp_err_t err = marker_set(path);
    if (err == ESP_OK) {
        err = rg_wav_writer_open(&s.wav, path, RG_SAMPLE_RATE_HZ);
        if (err != ESP_OK) {
            marker_set(NULL);
        }
    }
    if (err == ESP_OK && xTaskCreate(writer_task, "rg_rec", WRITER_STACK, NULL, WRITER_PRIO, &s.task) != pdPASS) {
        rg_wav_writer_close(&s.wav);
        err = ESP_ERR_NO_MEM; /* file stays marked: rg_rec_recover() deletes it on the next boot */
    }
    if (err != ESP_OK) {
        vStreamBufferDelete(s.ring);
        s.ring = NULL;
    }
    return err;
}

size_t rg_rec_push(const void *pcm, size_t bytes)
{
    if (!s.ring) {
        return 0;
    }
    size_t n = xStreamBufferSend(s.ring, pcm, bytes, 0);
    s.stats.dropped_bytes += (uint32_t)(bytes - n);
    return n;
}

bool rg_rec_failed(void)
{
    return s.failed;
}

static esp_err_t wait_writer(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s.task && s.ring, ESP_ERR_INVALID_STATE, TAG, "not recording");
    s.waiter = xTaskGetCurrentTaskHandle();
    s.stop = true;
    if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms))) {
        /* Writer is stuck inside the SD driver. Leave everything allocated (it still owns the
         * buffers); the caller reboots and the marker makes the next boot repair the file. */
        ESP_LOGE(TAG, "writer did not finish within %lu ms", (unsigned long)timeout_ms);
        return RG_ERR_TIMEOUT;
    }
    s.task = NULL;
    vStreamBufferDelete(s.ring);
    s.ring = NULL;
    return ESP_OK;
}

esp_err_t rg_rec_stop(rg_rec_stats_t *stats, uint32_t timeout_ms)
{
    esp_err_t err = wait_writer(timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    /* Verify whatever reached the card, even after a write error: a card that filled up
     * mid-message still holds a valid, truncated message. */
    if (s.close_ok) {
        rg_wav_info_t info;
        s.stats.file_valid = rg_wav_probe(s.path, &info) == ESP_OK && rg_wav_is_playable(&info) &&
                             info.header_consistent && info.data_bytes == s.stats.data_bytes;
    }
    err = s.result;
    if (err == ESP_OK && !s.stats.file_valid) {
        ESP_LOGE(TAG, "verify failed for %s (%u B written)", s.path, (unsigned)s.stats.data_bytes);
        err = RG_ERR_VERIFY;
    }
    if (s.stats.file_valid) {
        marker_set(NULL);
    }
    if (stats) {
        *stats = s.stats;
    }
    return err;
}

esp_err_t rg_rec_discard(uint32_t timeout_ms)
{
    esp_err_t err = wait_writer(timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    if (unlink(s.path) != 0) {
        ESP_LOGW(TAG, "could not delete %s", s.path);
        return ESP_FAIL; /* marker stays: the next boot's recovery deletes it */
    }
    return marker_set(NULL);
}

esp_err_t rg_rec_recover(uint32_t min_data_bytes, rg_rec_recovery_t *out)
{
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; /* namespace never written: nothing pending */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs");
    size_t len = sizeof(out->path);
    err = nvs_get_str(h, NVS_KEY, out->path, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs get");
    out->found = true;
    err = rg_wav_repair(out->path, min_data_bytes, &out->action, &out->data_bytes);
    if (err == ESP_ERR_NOT_FOUND) {
        out->action = RG_WAV_REPAIR_DELETED; /* never created: nothing to repair */
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = marker_set(NULL);
    }
    return err;
}
