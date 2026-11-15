#include "gb_audio.h"
#include <string.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/task.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_err.h"
#include "rg_mic_conv.h"

#define FRAMES        RG_AUDIO_DMA_FRAMES
#define TASK_PRIO     10 /* above the SD writer (5), UI (3) and LED (2): capture never misses DMA */
#define TASK_STACK    6144
#define BEEP_MS       400
#define BEEP_DBFS_Q15 8231 /* -12 dBFS */
#define BEEP_FADE     160  /* 10 ms fade in/out: no click */

static const char *TAG = "gb_audio";
static QueueHandle_t s_events, s_cmd;
static volatile bool s_hangup, s_busy;
static int32_t s_raw[FRAMES];
static int16_t s_pcm[FRAMES];

/* One period of 1 kHz at 16 kHz, unit amplitude in Q15. */
static const int16_t SINE16[16] = {0, 12539,  23170,  30273,  32767,  30273,  23170,  12539,
                                   0, -12539, -23170, -30273, -32767, -30273, -23170, -12539};

static void post(gb_audio_ev_type_t type, uint32_t value, esp_err_t err, const rg_rec_stats_t *stats)
{
    gb_audio_event_t ev = {.type = type, .value = value, .err = err};
    if (stats) {
        ev.stats = *stats;
    }
    xQueueSend(s_events, &ev, portMAX_DELAY);
}

static bool hung_up(void *ctx)
{
    (void)ctx;
    return s_hangup;
}

/* Missing files are skipped (instruction is optional); a wrong format is skipped with a warning
 * so a bad greeting never blocks the guest from leaving a message. */
static esp_err_t play_optional(const char *path)
{
    esp_err_t err = rg_audio_play_wav(path, hung_up, NULL);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == RG_ERR_BAD_FORMAT) {
        ESP_LOGW(TAG, "%s skipped: not 16 kHz mono 16-bit", path);
        return ESP_OK;
    }
    return err;
}

/* beep.wav if present, otherwise a built-in 400 ms 1 kHz tone with fades. */
static esp_err_t play_beep(void)
{
    rg_wav_info_t info;
    if (rg_wav_probe(RG_FILE_BEEP, &info) == ESP_OK && rg_wav_is_playable(&info)) {
        return play_optional(RG_FILE_BEEP);
    }
    const uint32_t total = RG_SAMPLE_RATE_HZ * BEEP_MS / 1000;
    int16_t buf[FRAMES];
    for (uint32_t done = 0; done < total;) {
        if (s_hangup) {
            return RG_ERR_ABORTED;
        }
        uint32_t n = total - done < FRAMES ? total - done : FRAMES;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t k = done + i;
            int32_t env = k < BEEP_FADE ? (int32_t)k : (total - k < BEEP_FADE ? (int32_t)(total - k) : BEEP_FADE);
            buf[i] = (int16_t)((int32_t)SINE16[k % 16] * BEEP_DBFS_Q15 / 32768 * env / BEEP_FADE);
        }
        esp_err_t err = rg_audio_write(buf, n);
        if (err != ESP_OK) {
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

/* Greeting, instruction, beep; returns once the beep has completely left the DMA. */
static esp_err_t intro(void)
{
    esp_err_t err = rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 20); /* amp leaves standby */
    if (err == ESP_OK) {
        err = play_optional(RG_FILE_GREETING);
    }
    if (err == ESP_OK) {
        post(GB_EV_INSTRUCTION, 0, ESP_OK, NULL);
        err = play_optional(RG_FILE_INSTRUCTION);
    }
    if (err == ESP_OK) {
        post(GB_EV_BEEP, 0, ESP_OK, NULL);
        err = play_beep();
    }
    if (err == ESP_OK) {
        err = rg_audio_drain();
    }
    if (err == ESP_OK && s_hangup) {
        err = RG_ERR_ABORTED;
    }
    return err;
}

/* Capture until hang-up, the time limit, or a recorder failure. Returns samples kept. */
static uint32_t capture(const gb_session_t *ss)
{
    const uint32_t max_samples = ss->max_seconds * RG_SAMPLE_RATE_HZ;
    uint32_t samples = 0, next_tick = RG_SAMPLE_RATE_HZ;
    rg_mic_conv_t conv;
    rg_mic_conv_init(&conv, ss->mic_gain_db, CONFIG_RG_MIC_DC_BLOCK);
    rg_audio_rx_flush(); /* first stored sample is the first one after the beep */
    post(GB_EV_REC_STARTED, ss->max_seconds, ESP_OK, NULL);

    while (!s_hangup && !rg_rec_failed()) {
        esp_task_wdt_reset();
        size_t got;
        esp_err_t err = rg_audio_read(s_raw, FRAMES, &got, 500);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read: %s", rg_err_name(err));
            break;
        }
        size_t n = got < max_samples - samples ? got : max_samples - samples;
        rg_mic_conv_run(&conv, s_raw, 1, s_pcm, n);
        rg_rec_push(s_pcm, n * sizeof(int16_t));
        samples += (uint32_t)n;
        while (samples >= next_tick && next_tick <= max_samples) {
            post(GB_EV_REC_TICK, (max_samples - next_tick) / RG_SAMPLE_RATE_HZ, ESP_OK, NULL);
            next_tick += RG_SAMPLE_RATE_HZ;
        }
        if (samples >= max_samples) {
            post(GB_EV_REC_LIMIT, ss->max_seconds, ESP_OK, NULL);
            break;
        }
    }
    if (conv.clipped) {
        ESP_LOGI(TAG, "%lu samples clipped: consider a lower mic_gain_db", (unsigned long)conv.clipped);
    }
    return samples;
}

static void run_session(const gb_session_t *ss)
{
    esp_task_wdt_add(NULL);
    esp_err_t err = rg_rec_start(ss->path, ss->ring_bytes, ss->min_free_bytes);
    if (err != ESP_OK) {
        post(GB_EV_FAILED, 0, err, NULL);
        goto out;
    }
    err = rg_audio_open(RG_AUDIO_DUPLEX, false);
    if (err == ESP_OK) {
        err = intro();
    }
    if (err != ESP_OK) {
        rg_audio_close();
        esp_err_t d = rg_rec_discard(ss->save_timeout_ms);
        if (d == RG_ERR_TIMEOUT) {
            post(GB_EV_FAILED, 0, d, NULL);
        } else {
            post(err == RG_ERR_ABORTED ? GB_EV_DISCARDED : GB_EV_FAILED, 0, err, NULL);
        }
        goto out;
    }

    uint32_t samples = capture(ss);
    rg_audio_close(); /* clocks stop and pins float before the SD card gives the rail back */

    if (samples == 0 || samples < ss->min_seconds * RG_SAMPLE_RATE_HZ) { /* never keep an empty file */
        esp_err_t d = rg_rec_discard(ss->save_timeout_ms);
        post(d == RG_ERR_TIMEOUT ? GB_EV_FAILED : GB_EV_DISCARDED, samples, d, NULL);
        goto out;
    }
    rg_rec_stats_t stats;
    err = rg_rec_stop(&stats, ss->save_timeout_ms);
    if (stats.dropped_bytes) {
        ESP_LOGW(TAG, "%lu bytes dropped: ring buffer too small for this card", (unsigned long)stats.dropped_bytes);
    }
    post(GB_EV_SAVED, samples, err, &stats);
out:
    esp_task_wdt_delete(NULL);
    s_busy = false;
}

static void audio_task(void *arg)
{
    (void)arg;
    gb_session_t session;
    for (;;) {
        if (xQueueReceive(s_cmd, &session, portMAX_DELAY) == pdTRUE) {
            run_session(&session);
        }
    }
}

esp_err_t gb_audio_init(QueueHandle_t events)
{
    s_events = events;
    s_cmd = xQueueCreate(1, sizeof(gb_session_t));
    if (!s_cmd) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(audio_task, "gb_audio", TASK_STACK, NULL, TASK_PRIO, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t gb_audio_start_session(const gb_session_t *session)
{
    if (s_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    s_hangup = false;
    s_busy = true;
    if (xQueueSend(s_cmd, session, 0) != pdTRUE) {
        s_busy = false;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void gb_audio_hang_up(void)
{
    s_hangup = true;
}

bool gb_audio_busy(void)
{
    return s_busy;
}
