/*
 * Screen drawing for the 128x64 OLED: model in, frame buffer out. No FreeRTOS or I2C here, so the
 * same code also renders on a PC for layout checks.
 */
#include <stdio.h>
#include "gb_ui.h"
#include "rg_oled.h"

#define ERROR_TITLE_BASE 30
#define ERROR_HINT_BASE  50
#define WIFI_TITLE_BASE  15
#define WIFI_ROW_BASE    33 /* three rows, 14 px apart */

static void draw_error(const gb_ui_model_t *m, int dx)
{
    /* Hints in capitals: in the small font a lower-case i reads like l, the dot of İ stays clear. */
    static const char *const TITLE[] = {"KAYIT HATASI", "KART HATASI", "KART DOLU", "CİHAZ ARIZASI", "WIFI.TXT YOK"};
    static const char *const HINT[] = {"TEKRAR DENEYİN", "KARTI DENETLEYİN", "KARTI DEĞİŞTİRİN", "YENİDEN BAŞLATIN",
                                       "KARTA EKLEYİN"};
    rg_oled_font_text_center(&rg_font_head, ERROR_TITLE_BASE, TITLE[m->error], dx);
    rg_oled_font_text_center(&rg_font_small, ERROR_HINT_BASE, HINT[m->error], dx);
}

/* Listening mode: network name, address, and what is playing. */
static void draw_wifi(const gb_ui_model_t *m, int dx)
{
    char row[48];
    rg_oled_font_text_center(&rg_font_head, WIFI_TITLE_BASE, "WI-FI AÇIK", dx);
    snprintf(row, sizeof(row), "Ağ: %s", m->wifi_ssid);
    rg_oled_font_text_center(&rg_font_small, WIFI_ROW_BASE, row, dx);
    rg_oled_font_text_center(&rg_font_small, WIFI_ROW_BASE + 14, "192.168.4.1", dx);
    if (m->wifi_playing) {
        snprintf(row, sizeof(row), "Çalıyor: mesaj %lu", (unsigned long)m->wifi_playing);
    } else if (m->wifi_clients) {
        snprintf(row, sizeof(row), "Bağlı: %lu telefon", (unsigned long)m->wifi_clients);
    } else {
        snprintf(row, sizeof(row), "Telefonla bağlanın");
    }
    rg_oled_font_text_center(&rg_font_small, WIFI_ROW_BASE + 28, row, dx);
}

bool gb_ui_draw(const gb_ui_model_t *m)
{
    char count[12], timer[8];
    const int dx = (int)(m->shift % 3) - 1; /* 1 px burn-in shift per session */
    const char *caption = m->low_space ? "AZ YER\nKALDI!" : "MESAJ";
    if (m->total_known) {
        snprintf(count, sizeof(count), "#%03lu", (unsigned long)(m->total % 100000));
    } else {
        snprintf(count, sizeof(count), "#---");
    }
    rg_oled_clear();
    switch (m->screen) {
    case GB_UI_STANDBY:
        if (m->standby == RG_STANDBY_OFF) {
            return false;
        }
        if (m->standby == RG_STANDBY_FULL) {
            rg_oled_action_screen(count, caption, NULL, "AHİZEYİ", "KALDIRIN", dx);
        } else {
            rg_oled_action_screen(count, NULL, NULL, NULL, NULL, dx); /* fewest lit pixels */
        }
        break;
    case GB_UI_WAKING:
    case GB_UI_GREETING:
    case GB_UI_INSTRUCTION: /* the guest is listening: the standby prompt stays until the beep */
        rg_oled_action_screen(count, caption, NULL, "AHİZEYİ", "KALDIRIN", dx);
        break;
    case GB_UI_RECORDING:
        snprintf(timer, sizeof(timer), "%lu:%02lu", (unsigned long)(m->seconds_left / 60 % 10),
                 (unsigned long)(m->seconds_left % 60)); /* max_seconds <= 300 */
        rg_oled_action_screen(count, NULL, timer, "KONUŞMAYA", "BAŞLAYIN", dx);
        break;
    case GB_UI_MAX_DURATION:
    case GB_UI_HANG_UP:
        rg_oled_action_screen(count, caption, NULL, "AHİZEYİ", "KAPATIN", dx);
        break;
    case GB_UI_SAVING:
        rg_oled_action_screen(count, caption, NULL, "LÜTFEN", "BEKLEYİN", dx); /* KAYDEDİLİYOR is too wide at 13 px */
        break;
    case GB_UI_SUCCESS:
        rg_oled_action_screen(count, caption, NULL, "MESAJINIZ", "KAYDEDİLDİ", dx);
        break;
    case GB_UI_ERROR:
        draw_error(m, dx);
        break;
    case GB_UI_WIFI:
        draw_wifi(m, dx);
        break;
    }
    return true;
}
