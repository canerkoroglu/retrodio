/* T2: SSD1306 presence, flush time, font legibility at 128x64, contrast. */
#include <stdio.h>
#include "hwtest.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rg_oled.h"

static void screen(const char *a, const char *b, const char *c, int big_line)
{
    rg_oled_clear();
    if (a) {
        rg_oled_font_text_center(&rg_font_big, 20, a, 0);
    }
    if (big_line) {
        rg_oled_font_text_center(&rg_font_big, 44, b, 0);
    } else {
        if (b) {
            rg_oled_font_text_center(&rg_font_small, 40, b, 0);
        }
        if (c) {
            rg_oled_font_text_center(&rg_font_small, 56, c, 0);
        }
    }
    rg_oled_flush();
}

tr_t t2_oled(void)
{
    if (!rg_oled_present() && rg_oled_init(false) != ESP_OK) {
        hw_result(2, TR_FAIL, "ack=no addr=0x%02X hint=check_SDA_SCL_wiring_power_and_address",
                  CONFIG_RG_OLED_I2C_ADDR);
        return TR_FAIL;
    }

    rg_oled_clear();
    rg_oled_rect(0, 0, RG_OLED_WIDTH, RG_OLED_HEIGHT, false);
    rg_oled_font_text_center(&rg_font_big, 37, "OLED 128x64 OK", 0);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = rg_oled_flush();
    uint32_t flush_us = (uint32_t)(esp_timer_get_time() - t0);
    printf("Frame flush: %lu us (%s)\n", (unsigned long)flush_us, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1500));

    rg_oled_clear();
    char row[26];
    for (int line = 0, ch = 0x20; line < 4; line++) { /* 5x7 diagnostics font, top half */
        int n = 0;
        for (; n < 25 && ch <= 0x7E; n++, ch++) {
            row[n] = (char)ch;
        }
        row[n] = 0;
        rg_oled_text(0, line * 8, row, 1);
    }
    rg_oled_font_text(&rg_font_small, 0, 45, "Ağ çiçek ığdır öşü ÇĞİÖŞÜ");
    rg_oled_font_text(&rg_font_big, 0, 62, "ÇĞİÖŞÜ 0123456789");
    rg_oled_flush();
    printf("Showing the character sets...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* The real UI screens, same layout and large font as the guestbook. */
    const struct {
        const char *count, *caption, *timer, *top, *bottom;
        uint32_t ms;
    } SCREENS[] = {
        {"#024", "MESAJ", NULL, "AHİZEYİ", "KALDIRIN", 2500},
        {"#024", NULL, "2:59", "KONUŞMAYA", "BAŞLAYIN", 2500},
        {"#024", "MESAJ", NULL, "AHİZEYİ", "KAPATIN", 2000},
        {"#024", "MESAJ", NULL, "LÜTFEN", "BEKLEYİN", 1500},
        {"#025", "MESAJ", NULL, "MESAJINIZ", "KAYDEDİLDİ", 2500},
        {"#025", "AZ YER\nKALDI!", NULL, "AHİZEYİ", "KALDIRIN", 2000},
    };
    for (size_t i = 0; i < sizeof(SCREENS) / sizeof(SCREENS[0]); i++) {
        rg_oled_clear();
        rg_oled_action_screen(SCREENS[i].count, SCREENS[i].caption, SCREENS[i].timer, SCREENS[i].top, SCREENS[i].bottom,
                              0);
        rg_oled_flush();
        vTaskDelay(pdMS_TO_TICKS(SCREENS[i].ms));
    }
    screen("KAYIT HATASI", "Tekrar deneyin", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));

    printf("Contrast sweep 0x01 -> 0xFF...\n");
    screen("CONTRAST", "SWEEP", NULL, 0);
    const uint8_t levels[] = {0x01, 0x10, 0x30, 0x60, 0xA0, 0xFF};
    for (size_t i = 0; i < sizeof(levels); i++) {
        rg_oled_set_contrast(levels[i]);
        printf("  contrast 0x%02X\n", levels[i]);
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    rg_oled_set_contrast(CONFIG_RG_OLED_CONTRAST);

    bool readable = hw_ask_yes("Were all screens sharp and fully readable (no cut-off text)?");
    hw_result(2, readable ? TR_PASS : TR_FAIL, "ack=yes addr=0x%02X flush_us=%lu readable=%s", CONFIG_RG_OLED_I2C_ADDR,
              (unsigned long)flush_us, readable ? "yes" : "no");
    return readable ? TR_PASS : TR_FAIL;
}
