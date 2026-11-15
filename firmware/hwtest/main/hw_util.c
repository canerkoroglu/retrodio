#include "hwtest.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "rg_board.h"
#include "rg_oled.h"
#include "rg_audio.h"
#include "rg_catalog.h"
#include "rg_err.h"
#include "rg_eventlog.h"
#include "rg_led.h"
#include "rg_recorder.h"
#include "rg_storage.h"

static const char *const RESULT_NAMES[] = {"PASS", "FAIL", "INFO"};
static uint32_t s_sd_khz;
static struct {
    rg_rec_recovery_t recovery;
    esp_err_t recover_err;
    int settings_warnings;
    char warnings[384];
    rg_catalog_state_t catalog;
    esp_err_t catalog_err;
} s_svc;

rg_settings_t g_settings;
rg_boot_info_t g_boot;

void hw_console_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    cfg.tx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdout, NULL, _IONBF, 0);
}

int hw_key(uint32_t timeout_ms)
{
    uint8_t c;
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        if (usb_serial_jtag_read_bytes(&c, 1, ticks) != 1) {
            return -1;
        }
        if (c != '\r' && c != '\n') {
            return c;
        }
    }
}

bool hw_key_pressed(void)
{
    return hw_key(0) >= 0;
}

bool hw_ask_yes(const char *question)
{
    printf("%s [y/n] ", question);
    for (;;) {
        int c = hw_key(UINT32_MAX);
        if (c == 'y' || c == 'Y') {
            printf("y\n");
            return true;
        }
        if (c == 'n' || c == 'N') {
            printf("n\n");
            return false;
        }
    }
}

void hw_result(int test, tr_t r, const char *fmt, ...)
{
    printf("\nRESULT T%d %s ", test, RESULT_NAMES[r]);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* Boot services run once per mount, exactly as the final application will. */
static void card_services(void)
{
    rg_storage_ensure_dirs();
    s_svc.recover_err = rg_rec_recover(RG_BYTES_PER_SECOND, &s_svc.recovery);
    s_svc.settings_warnings = rg_settings_load(RG_FILE_CONFIG, &g_settings, s_svc.warnings, sizeof(s_svc.warnings));
    rg_led_set_max_brightness((uint8_t)g_settings.led_brightness);
    rg_oled_set_contrast((uint8_t)g_settings.oled_contrast);
    rg_audio_set_volume_db(g_settings.volume_db);
    s_svc.catalog_err = rg_catalog_open(&s_svc.catalog);
    rg_eventlog("mount %lu kHz, fw %s, reset %s, crashes %lu", (unsigned long)s_sd_khz, g_boot.version,
                rg_reset_reason_name(g_boot.reason), (unsigned long)g_boot.consecutive_crashes);
    if (s_svc.recovery.found) {
        rg_eventlog("recovered %s: %s, %lu B", s_svc.recovery.path,
                    s_svc.recovery.action == RG_WAV_REPAIR_FIXED     ? "header repaired"
                    : s_svc.recovery.action == RG_WAV_REPAIR_DELETED ? "deleted (too short)"
                                                                     : "already consistent",
                    (unsigned long)s_svc.recovery.data_bytes);
    }
    if (s_svc.settings_warnings > 0) {
        rg_eventlog("config.txt: %d warning(s)\n%s", s_svc.settings_warnings, s_svc.warnings);
    }
    if (s_svc.catalog_err == ESP_OK && s_svc.catalog.minutes_left < (uint32_t)g_settings.low_space_minutes) {
        rg_eventlog("low space: %lu min of recording left", (unsigned long)s_svc.catalog.minutes_left);
    }
}

uint32_t hw_sd_mount(void)
{
    if (rg_storage_mounted()) {
        return s_sd_khz;
    }
    const uint32_t tries[] = {CONFIG_RG_SD_FREQ_KHZ, 10000, 4000, 400};
    for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
        if (i > 0 && tries[i] >= CONFIG_RG_SD_FREQ_KHZ) {
            continue;
        }
        if (rg_storage_mount(tries[i]) == ESP_OK) {
            s_sd_khz = tries[i];
            card_services();
            return s_sd_khz;
        }
    }
    s_sd_khz = 0;
    return 0;
}

void hw_print_card_services(void)
{
    printf("Card services:\n");
    if (s_svc.recovery.found) {
        printf("  power-cut recovery: %s -> %s (%lu B audio)\n", s_svc.recovery.path,
               s_svc.recovery.action == RG_WAV_REPAIR_FIXED     ? "header repaired"
               : s_svc.recovery.action == RG_WAV_REPAIR_DELETED ? "deleted, too short"
                                                                : "already consistent",
               (unsigned long)s_svc.recovery.data_bytes);
    } else {
        printf("  power-cut recovery: nothing pending (%s)\n", rg_err_name(s_svc.recover_err));
    }
    if (s_svc.settings_warnings < 0) {
        printf("  config.txt: not present, using firmware defaults\n");
    } else {
        printf("  config.txt: loaded, %d warning(s)\n%s", s_svc.settings_warnings, s_svc.warnings);
    }
    printf("  settings: volume %d dB, mic gain %d dB, max %d s, LED %d, contrast %d, standby %s\n",
           g_settings.volume_db, g_settings.mic_gain_db, g_settings.max_seconds, g_settings.led_brightness,
           g_settings.oled_contrast,
           g_settings.standby_screen == RG_STANDBY_FULL      ? "full"
           : g_settings.standby_screen == RG_STANDBY_MINIMAL ? "minimal"
                                                             : "off");
    if (s_svc.catalog_err == ESP_OK) {
        printf("  catalog: %lu messages, %lu damaged, next MSG_%06lu, %lu min of recording left%s\n",
               (unsigned long)s_svc.catalog.count, (unsigned long)s_svc.catalog.invalid,
               (unsigned long)s_svc.catalog.next_index, (unsigned long)s_svc.catalog.minutes_left,
               s_svc.catalog.minutes_left < (uint32_t)g_settings.low_space_minutes ? "  ** LOW SPACE **" : "");
    } else {
        printf("  catalog: %s\n", rg_err_name(s_svc.catalog_err));
    }
}

void hw_oled_status(const char *line1, const char *line2)
{
    if (!rg_oled_present()) {
        return;
    }
    rg_oled_clear();
    if (line1) {
        rg_oled_text_center(4, line1, 1);
    }
    if (line2) {
        rg_oled_text_center(18, line2, 1);
    }
    rg_oled_flush();
}

void hw_tone(int16_t *out, int n, int dbfs)
{
    static uint32_t phase;
    float amp = 32767.0f * powf(10.0f, (float)dbfs / 20.0f);
    for (int i = 0; i < n; i++, phase++) {
        out[i] = (int16_t)lroundf(amp * sinf(2.0f * (float)M_PI * (float)(phase % 16) / 16.0f));
    }
}
