#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rg_settings.h"

/*
 * OLED UI service. The state machine publishes a small model; a low-priority task renders it
 * (~13 ms I2C flush) so the display never delays audio or SD work.
 */

typedef enum {
    GB_UI_STANDBY,
    GB_UI_WAKING,
    GB_UI_GREETING,
    GB_UI_INSTRUCTION,
    GB_UI_RECORDING,
    GB_UI_MAX_DURATION,
    GB_UI_SAVING,
    GB_UI_SUCCESS,
    GB_UI_ERROR,
    GB_UI_HANG_UP,
} gb_ui_screen_t;

typedef enum {
    GB_UI_ERR_SAVE,    /* KAYIT HATASI */
    GB_UI_ERR_CARD,    /* KART HATASI: missing / unreadable card */
    GB_UI_ERR_FULL,    /* KART DOLU */
    GB_UI_ERR_SERVICE, /* SERVIS GEREKLI: safe mode after repeated crashes */
} gb_ui_error_t;

typedef struct {
    gb_ui_screen_t screen;
    uint32_t total;        /* saved messages */
    bool total_known;      /* false: card not read yet / card error */
    uint32_t seconds_left; /* recording countdown */
    gb_ui_error_t error;
    bool low_space; /* standby hint: card almost full */
    rg_standby_screen_t standby;
    uint32_t shift; /* burn-in pixel shift seed (session counter) */
} gb_ui_model_t;

void gb_ui_init(void);
/** Publish a new model (non-blocking; rendering happens in the UI task). */
void gb_ui_show(const gb_ui_model_t *model);
/** Render in the caller and wait for the flush (used right before deep sleep). */
void gb_ui_show_sync(const gb_ui_model_t *model);
