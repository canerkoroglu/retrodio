#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rg_recorder.h"

/*
 * Audio session engine (own task, highest application priority).
 *
 * One session = greeting -> instruction -> beep -> recording -> save, on one I2S DUPLEX
 * channel pair that stays open throughout: the mic is clocked and warm long before the beep
 * ends, so recording starts with the first sample after the beep. The WAV file is created
 * before the greeting (SD latency never eats the guest's first words) and deleted again if the
 * guest hangs up before the beep finishes or speaks for less than min_seconds.
 */

typedef enum {
    GB_EV_INSTRUCTION, /* greeting finished, instruction playing */
    GB_EV_BEEP,        /* instruction finished, beep playing */
    GB_EV_REC_STARTED, /* beep fully played; value = max seconds */
    GB_EV_REC_TICK,    /* value = seconds left (sent once per second) */
    GB_EV_REC_LIMIT,   /* max duration reached; saving follows automatically */
    GB_EV_SAVED,       /* err + stats: finalized (err may be RG_ERR_CARD_FULL with a valid file) */
    GB_EV_DISCARDED,   /* hang-up before/just after the beep: nothing kept */
    GB_EV_FAILED,      /* session could not start or playback failed; err says why */
} gb_audio_ev_type_t;

typedef struct {
    gb_audio_ev_type_t type;
    uint32_t value;
    esp_err_t err;
    rg_rec_stats_t stats;
} gb_audio_event_t;

typedef struct {
    char path[96];
    uint32_t max_seconds;
    uint32_t min_seconds;
    int mic_gain_db;
    size_t ring_bytes;
    uint64_t min_free_bytes;
    uint32_t save_timeout_ms;
} gb_session_t;

/** Create the audio task; events are posted to `events`. */
esp_err_t gb_audio_init(QueueHandle_t events);
/** Start a session (non-blocking). */
esp_err_t gb_audio_start_session(const gb_session_t *session);
/** Handset down: abort the intro (discard) or end the recording (save). Non-blocking. */
void gb_audio_hang_up(void);
bool gb_audio_busy(void);
