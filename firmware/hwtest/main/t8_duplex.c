/*
 * T8: playback -> recording transition on the single I2S controller.
 *  A) DUPLEX: TX+RX paired from the start (shared BCLK/WS), mic warms up during playback,
 *     recording starts right after the beep has drained. No channel teardown.
 *  B) SEQUENTIAL: TX-only channel, delete it, create an RX-only channel, wait for valid data.
 * Each mode: one measured run with a 3 s WAV on the card, then a 20-cycle stress loop.
 */
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

#define FRAMES      RG_AUDIO_DMA_FRAMES
#define BEEP_MS     400
#define STRESS_RUNS 20

static int32_t s_raw[FRAMES];
static int16_t s_pcm[FRAMES];
static int16_t s_tone[FRAMES];

typedef struct {
    uint32_t gap_us;         /* beep fully played -> first RX sample that will be stored */
    uint32_t first_valid_us; /* beep fully played -> first non-zero mic word */
    uint32_t overflows;
    esp_err_t err;
} run_t;

static esp_err_t play_beep(void)
{
    /* The amp is powered with the rail; it leaves standby once BCLK runs (> 7.5 ms of silence). */
    esp_err_t err = rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 20);
    for (uint32_t n = 0; err == ESP_OK && n < RG_SAMPLE_RATE_HZ * BEEP_MS / 1000; n += FRAMES) {
        hw_tone(s_tone, FRAMES, -12);
        err = rg_audio_write(s_tone, FRAMES);
    }
    if (err == ESP_OK) {
        err = rg_audio_drain();
    }
    return err;
}

/* Capture `ms` of mono audio after the transition; optionally to a WAV. */
static void capture(run_t *r, int64_t t_beep_done, uint32_t ms, const char *wav)
{
    rg_mic_conv_t conv;
    rg_mic_conv_init(&conv, g_settings.mic_gain_db, CONFIG_RG_MIC_DC_BLOCK);
    bool recording = wav && rg_rec_start(wav, 64 * 1024, 512 * 1024) == ESP_OK;
    bool first = true, valid = false;
    for (uint32_t total = 0; total < RG_SAMPLE_RATE_HZ * ms / 1000;) {
        size_t got;
        r->err = rg_audio_read(s_raw, FRAMES, &got, 500);
        if (r->err != ESP_OK) {
            break;
        }
        int64_t now = esp_timer_get_time();
        if (first) {
            /* The buffer just returned started FRAMES/fs before `now`. */
            int64_t start = now - (int64_t)got * 1000000 / RG_SAMPLE_RATE_HZ;
            r->gap_us = start > t_beep_done ? (uint32_t)(start - t_beep_done) : 0;
            first = false;
        }
        for (size_t i = 0; i < got && !valid; i++) {
            if (s_raw[i] != 0) {
                valid = true;
                r->first_valid_us = (uint32_t)(now - t_beep_done);
            }
        }
        if (recording) {
            rg_mic_conv_run(&conv, s_raw, 1, s_pcm, got);
            rg_rec_push(s_pcm, got * sizeof(int16_t));
        }
        total += got;
    }
    if (recording) {
        rg_rec_stats_t st;
        esp_err_t e = rg_rec_stop(&st, 5000);
        if (e == RG_ERR_TIMEOUT) {
            esp_restart(); /* stuck SD: reboot, the file is repaired on the next mount */
        }
        if (r->err == ESP_OK) {
            r->err = e;
        }
    }
    r->overflows = rg_audio_rx_overflows();
}

static run_t run_duplex(uint32_t capture_ms, const char *wav)
{
    run_t r = {0};
    r.err = rg_audio_open(RG_AUDIO_DUPLEX, false);
    if (r.err != ESP_OK) {
        return r;
    }
    r.err = play_beep();
    int64_t t = esp_timer_get_time();
    if (r.err == ESP_OK) {
        rg_audio_rx_flush(); /* recording starts "now" */
        capture(&r, t, capture_ms, wav);
    }
    rg_audio_close();
    return r;
}

static run_t run_sequential(uint32_t capture_ms, const char *wav)
{
    run_t r = {0};
    r.err = rg_audio_open(RG_AUDIO_TX, false);
    if (r.err != ESP_OK) {
        return r;
    }
    r.err = play_beep();
    int64_t t = esp_timer_get_time();
    rg_audio_close();
    if (r.err == ESP_OK) {
        r.err = rg_audio_open(RG_AUDIO_RX, false);
    }
    if (r.err == ESP_OK) {
        capture(&r, t, capture_ms, wav);
    }
    rg_audio_close();
    return r;
}

typedef run_t (*mode_fn)(uint32_t, const char *);

static bool evaluate(const char *name, mode_fn fn, const char *wav, uint32_t *gap_ms, uint32_t *valid_ms)
{
    printf("\n[%s] measured run%s\n", name, wav ? " (3 s WAV)" : "");
    hw_oled_status("TX/RX TEST", name);
    if (wav) {
        unlink(wav);
    }
    run_t r = fn(3000, wav);
    printf("  err=%s gap=%lu us first_valid=%lu us overflows=%lu\n", rg_err_name(r.err), (unsigned long)r.gap_us,
           (unsigned long)r.first_valid_us, (unsigned long)r.overflows);
    bool ok = r.err == ESP_OK && r.first_valid_us > 0;
    uint32_t worst_gap = r.gap_us, worst_valid = r.first_valid_us;

    printf("[%s] stress: %d cycles\n", name, STRESS_RUNS);
    for (int i = 0; i < STRESS_RUNS && ok; i++) {
        run_t s = fn(300, NULL);
        ok = s.err == ESP_OK && s.first_valid_us > 0;
        worst_gap = s.gap_us > worst_gap ? s.gap_us : worst_gap;
        worst_valid = s.first_valid_us > worst_valid ? s.first_valid_us : worst_valid;
        if (!ok) {
            printf("  cycle %d failed: %s\n", i + 1, rg_err_name(s.err));
        }
    }
    *gap_ms = worst_gap / 1000;
    *valid_ms = worst_valid / 1000;
    printf("  %s: worst gap %lu ms, worst first-valid %lu ms\n", ok ? "OK" : "FAILED", (unsigned long)*gap_ms,
           (unsigned long)*valid_ms);
    return ok;
}

tr_t t8_duplex(void)
{
    rg_audio_set_volume_db(g_settings.volume_db);
    bool sd = hw_sd_mount() != 0;
    printf("Hold the handset; you will hear %d short beeps per mode. Speak after a beep to fill the WAVs.\n",
           STRESS_RUNS + 1);
    uint32_t dg, dv, sg, sv;
    bool duplex_ok = evaluate("DUPLEX", run_duplex, sd ? RG_DIR_RECORDINGS "/TEST_DUPLEX.wav" : NULL, &dg, &dv);
    bool seq_ok = evaluate("SEQUENTIAL", run_sequential, sd ? RG_DIR_RECORDINGS "/TEST_SEQ.wav" : NULL, &sg, &sv);

    const char *pick = duplex_ok ? "duplex" : (seq_ok ? "sequential" : "none");
    tr_t r = duplex_ok || seq_ok ? TR_PASS : TR_FAIL;
    hw_result(8, r,
              "duplex=%s duplex_gap_ms=%lu duplex_valid_ms=%lu sequential=%s seq_gap_ms=%lu seq_valid_ms=%lu "
              "use=%s wavs=%s",
              duplex_ok ? "ok" : "fail", (unsigned long)dg, (unsigned long)dv, seq_ok ? "ok" : "fail",
              (unsigned long)sg, (unsigned long)sv, pick, sd ? "TEST_DUPLEX.wav,TEST_SEQ.wav" : "none");
    return r;
}
