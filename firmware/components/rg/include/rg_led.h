#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    RG_LED_OFF,
    RG_LED_SOLID,
    RG_LED_BLINK_SHORT,  /* 100 ms on / 400 ms off  (waking) */
    RG_LED_BLINK_FAST,   /* 125 ms on / 125 ms off  (saving, error) */
    RG_LED_BREATHE_SLOW, /* 4 s period              (greeting, instruction) */
    RG_LED_BREATHE,      /* 2.5 s period            (recording) */
} rg_led_effect_t;

typedef struct {
    uint8_t r, g, b;
} rg_rgb_t;

#define RG_RGB_BLUE   ((rg_rgb_t){0, 0, 255})
#define RG_RGB_YELLOW ((rg_rgb_t){255, 150, 0})
#define RG_RGB_GREEN  ((rg_rgb_t){0, 255, 0})
#define RG_RGB_CYAN   ((rg_rgb_t){0, 255, 255})
#define RG_RGB_RED    ((rg_rgb_t){255, 0, 0})
#define RG_RGB_WHITE  ((rg_rgb_t){255, 255, 255})

/** Create the RMT LED driver and the 50 Hz effect task (low priority, never blocks callers). */
esp_err_t rg_led_init(void);

/**
 * Change effect (non-blocking); colour is scaled to the brightness cap.
 * Any effect other than OFF holds the switched rail on; OFF sends black and releases it.
 */
void rg_led_set(rg_led_effect_t effect, rg_rgb_t color);

/** Runtime brightness cap (config.txt led_brightness), 1..255. */
void rg_led_set_max_brightness(uint8_t max);

/** Switch off, wait until black has been transmitted, release the rail (use before deep sleep). */
esp_err_t rg_led_off_sync(void);
