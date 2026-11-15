/* T2: SSD1306 presence, flush time, font legibility at 128x32, contrast. */
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
        rg_oled_text_center(0, a, 1);
    }
    if (big_line) {
        rg_oled_text_center(11, b, 2);
    } else {
        if (b) {
            rg_oled_text_center(12, b, 1);
        }
        if (c) {
            rg_oled_text_center(24, c, 1);
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
    rg_oled_rect(0, 0, 128, 32, false);
    rg_oled_text_center(12, "OLED 128x32 OK", 1);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = rg_oled_flush();
    uint32_t flush_us = (uint32_t)(esp_timer_get_time() - t0);
    printf("Frame flush: %lu us (%s)\n", (unsigned long)flush_us, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1500));

    rg_oled_clear();
    char row[26];
    for (int line = 0, ch = 0x20; line < 4; line++) {
        int n = 0;
        for (; n < 25 && ch <= 0x7E; n++, ch++) {
            row[n] = (char)ch;
        }
        row[n] = 0;
        rg_oled_text(0, line * 8, row, 1);
    }
    rg_oled_flush();
    printf("Showing the character set...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* The real UI screens (Turkish texts in ASCII). */
    screen("AUDIO GUESTBOOK", "TOPLAM: 024", "AHIZEYI KALDIRIN", 0);
    vTaskDelay(pdMS_TO_TICKS(2500));
    screen(NULL, "HOS GELDINIZ", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));
    screen("MESAJINIZI BIRAKIN", "03:00", NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(2500));
    screen("SURE DOLDU", "LUTFEN AHIZEYI", "YERINE KOYUN", 0);
    vTaskDelay(pdMS_TO_TICKS(2500));
    screen(NULL, "KAYDEDILIYOR...", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));
    screen("KAYIT BASARILI", "TOPLAM: 025", NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    screen(NULL, "KAYIT HATASI", NULL, 0);
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
