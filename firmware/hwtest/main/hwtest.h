#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rg_settings.h"
#include "rg_system.h"

/** Effective settings: menuconfig defaults overridden by /audio/config.txt once the card is mounted. */
extern rg_settings_t g_settings;
extern rg_boot_info_t g_boot;

typedef enum {
    TR_PASS,
    TR_FAIL,
    TR_INFO,
} tr_t;

/** Install the USB-Serial-JTAG driver so the console can read keys. */
void hw_console_init(void);

/** Wait up to timeout_ms for a key on the USB console; -1 on timeout. */
int hw_key(uint32_t timeout_ms);
/** Non-blocking: true if a key was pressed (consumes it). */
bool hw_key_pressed(void);
/** Ask a y/n question on the console (waits indefinitely). */
bool hw_ask_yes(const char *question);

/** Print the machine-readable result line the user pastes back. */
void hw_result(int test, tr_t r, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/**
 * Mount the SD card at the configured clock, falling back to slower clocks. On first mount it
 * also runs the boot services the final app uses: power-cut recovery, config.txt, catalog.
 * Returns kHz used or 0.
 */
uint32_t hw_sd_mount(void);
/** Print what the card services found on the last mount (recovery, settings, catalog). */
void hw_print_card_services(void);

/** OLED status helper: up to two lines, ignored if the OLED is absent. */
void hw_oled_status(const char *line1, const char *line2);

/** 1 kHz sine at the given level (dBFS), n samples, phase continues across calls. */
void hw_tone(int16_t *out, int n, int dbfs);

tr_t t1_board(void);
tr_t t2_oled(void);
tr_t t3_led(void);
tr_t t4_hook(void);
tr_t t5_sd(void);
tr_t t6_mic(void);
tr_t t7_amp(void);
tr_t t8_duplex(void);
tr_t t9_sleep(void);
/** Called from app_main when waking from a test-9 deep sleep. */
void t9_resume(void);
bool t9_active(void);
