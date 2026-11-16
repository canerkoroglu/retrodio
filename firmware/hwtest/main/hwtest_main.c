/*
 * Retro Audio Guestbook - Phase 2 hardware test firmware.
 * Flash once, open the serial monitor, pick a test by number.
 * Every test ends with a "RESULT Tn PASS|FAIL|INFO key=value ..." line to paste back.
 */
#include <stdio.h>
#include "hwtest.h"
#include "esp_system.h"
#include "rg_board.h"
#include "rg_hook.h"
#include "rg_audio.h"
#include "rg_led.h"
#include "rg_oled.h"
#include "rg_eventlog.h"
#include "rg_power.h"
#include "rg_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    const char *name;
    tr_t (*run)(void);
} test_t;

static const test_t TESTS[] = {
    {"Board & wiring (pull resistors, strapping, hook)", t1_board},
    {"OLED SSD1306 128x64", t2_oled},
    {"WS2812B status LED", t3_led},
    {"Hook switch & debounce", t4_hook},
    {"MicroSD read/write/latency", t5_sd},
    {"I2S microphone", t6_mic},
    {"MAX98357A playback", t7_amp},
    {"I2S TX->RX switching (duplex vs sequential)", t8_duplex},
    {"Deep sleep wake (hook)", t9_sleep},
};

void app_main(void)
{
    rg_board_init_safe_state(); /* rail off, CS released, LED low */
    hw_console_init();
    rg_system_init(&g_boot);
    rg_settings_defaults(&g_settings);
    rg_eventlog_set_boot(g_boot.boot_count, 0);
    rg_hook_init();
    rg_oled_init(g_boot.from_deep_sleep);

    printf("\nfw %s, boot #%lu, reset: %s, consecutive crashes: %lu%s%s\n", g_boot.version,
           (unsigned long)g_boot.boot_count, rg_reset_reason_name(g_boot.reason),
           (unsigned long)g_boot.consecutive_crashes, g_boot.safe_mode ? "  ** SAFE MODE **" : "",
           g_boot.coredump_present ? "  (crash dump stored: idf.py coredump-info)" : "");
    if (!g_boot.from_deep_sleep) {
        hw_oled_status("HW TEST", g_boot.version);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    if (t9_active()) {
        t9_resume(); /* returns only when test 9 is finished */
    }

    for (;;) {
        printf("\n=== Retro Guestbook hardware test %s ===\n", g_boot.version);
        for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
            printf("  %u  %s\n", (unsigned)(i + 1), TESTS[i].name);
        }
        printf("  r  reboot\nSelect: ");
        hw_oled_status("HW TEST", "SELECT ON CONSOLE");
        int c = hw_key(UINT32_MAX);
        if (c == 'r') {
            esp_restart();
        }
        int idx = c - '1';
        if (idx < 0 || idx >= (int)(sizeof(TESTS) / sizeof(TESTS[0]))) {
            continue;
        }
        printf("%c\n\n--- Test %d: %s ---\n", c, idx + 1, TESTS[idx].name);
        TESTS[idx].run();
        rg_system_mark_healthy(); /* a completed test proves the firmware is not crash-looping */
        /* Return shared peripherals to idle so tests stay independent (rail off afterwards). */
        rg_audio_close();
        rg_storage_unmount();
        rg_led_set(RG_LED_OFF, RG_RGB_WHITE);
        printf("Peripheral rail: %s\n", rg_power_is_on() ? "ON (leak!)" : "off");
    }
}
