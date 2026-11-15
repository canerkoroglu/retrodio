#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RG_STANDBY_FULL,    /* title + total + hint (spec layout) */
    RG_STANDBY_MINIMAL, /* total only: fewest lit pixels, lowest OLED current */
    RG_STANDBY_OFF,     /* panel sleep (~uA); count shown again on wake */
} rg_standby_screen_t;

typedef struct {
    int volume_db;      /* playback attenuation, -40..0 */
    int mic_gain_db;    /* 0..36 */
    int max_seconds;    /* recording limit, 10..300 */
    int led_brightness; /* WS2812B channel cap, 1..255 */
    int oled_contrast;  /* 1..255 */
    rg_standby_screen_t standby_screen;
    int low_space_minutes; /* warn when less recording time is left, 5..600 */
    int min_seconds;       /* shorter messages are discarded (hung up right after the beep), 0..10 */
} rg_settings_t;

/** Firmware defaults (from menuconfig). */
void rg_settings_defaults(rg_settings_t *s);

/**
 * Parse "key = value" lines ('#' starts a comment). Unknown keys and out-of-range values are
 * reported in `warnings` (may be NULL) and never applied unclamped; returns the warning count.
 */
int rg_settings_parse(const char *text, rg_settings_t *s, char *warnings, size_t warnings_len);

/** Defaults, then overrides from `path` if it exists. Returns the warning count (-1: no file). */
int rg_settings_load(const char *path, rg_settings_t *s, char *warnings, size_t warnings_len);
