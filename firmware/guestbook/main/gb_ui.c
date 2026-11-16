#include "gb_ui.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rg_oled.h"

#define UI_TASK_PRIO  3
#define UI_TASK_STACK 3072

static TaskHandle_t s_task;
static SemaphoreHandle_t s_draw; /* frame buffer + I2C are used by one renderer at a time */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static gb_ui_model_t s_model;
static bool s_panel_on = true;

static void center(int y, const char *s, int scale, int dx)
{
    rg_oled_text((RG_OLED_WIDTH - rg_oled_text_width(s, scale)) / 2 + dx, y, s, scale);
}

/* Message count for the rotated top-left label; '!' marks a card that is almost full. */
static void count_text(char *buf, size_t len, const gb_ui_model_t *m)
{
    if (m->total_known) {
        snprintf(buf, len, "%03lu%s", (unsigned long)m->total, m->low_space ? "!" : "");
    } else {
        snprintf(buf, len, "---");
    }
}

static void draw_error(const gb_ui_model_t *m)
{
    static const char *const TITLE[] = {"KAYIT HATASI", "KART HATASI", "KART DOLU", "SERVIS GEREKLI", "WIFI AYARI YOK"};
    static const char *const HINT[] = {"TEKRAR DENEYIN", "KARTI KONTROL EDIN", "KARTI DEGISTIRIN",
                                       "CIHAZI YENIDEN BASLATIN", "KARTA WIFI.TXT EKLEYIN"};
    center(4, TITLE[m->error], 1, 0);
    center(18, HINT[m->error], 1, 0);
}

/* Listening mode: network name, address, and what is playing, at scale 1 beside the count. */
static void draw_wifi(const gb_ui_model_t *m, const char *count, int dx)
{
    char row[40];
    const int x = RG_OLED_SIDE_WIDTH + dx;
    rg_oled_action_screen(count, NULL, NULL, 0, dx);
    snprintf(row, sizeof(row), "WIFI %s", m->wifi_ssid);
    rg_oled_text(x, 1, row, 1);
    rg_oled_text(x, 12, "192.168.4.1", 1);
    if (m->wifi_playing) {
        snprintf(row, sizeof(row), "CALIYOR: MESAJ %lu", (unsigned long)m->wifi_playing);
    } else if (m->wifi_clients) {
        snprintf(row, sizeof(row), "BAGLI: %lu CIHAZ", (unsigned long)m->wifi_clients);
    } else {
        snprintf(row, sizeof(row), "TELEFONLA BAGLANIN");
    }
    rg_oled_text(x, 23, row, 1);
}

/* Returns false if the panel should be switched off instead (standby "off"). */
static bool draw(const gb_ui_model_t *m)
{
    char count[16], line[8];
    int dx = (int)(m->shift % 3) - 1; /* 1 px burn-in shift per session */
    count_text(count, sizeof(count), m);
    rg_oled_clear();
    switch (m->screen) {
    case GB_UI_STANDBY:
        if (m->standby == RG_STANDBY_OFF) {
            return false;
        }
        if (m->standby == RG_STANDBY_FULL) {
            rg_oled_action_screen(count, "AHIZEYI", "KALDIRIN", 0, dx);
        } else {
            rg_oled_action_screen(count, NULL, NULL, 0, dx);
        }
        break;
    case GB_UI_WAKING:
    case GB_UI_GREETING:
    case GB_UI_INSTRUCTION: /* the guest is listening: the standby prompt stays until the beep */
        rg_oled_action_screen(count, "AHIZEYI", "KALDIRIN", 0, dx);
        break;
    case GB_UI_RECORDING:
        /* m:ss (max_seconds <= 300) in the bottom-right corner, beside BASLAYIN */
        snprintf(line, sizeof(line), "%lu:%02lu", (unsigned long)(m->seconds_left / 60 % 10),
                 (unsigned long)(m->seconds_left % 60));
        rg_oled_text(RG_OLED_WIDTH - rg_oled_text_width(line, 1), RG_OLED_HEIGHT - 7, line, 1);
        rg_oled_action_screen(count, "KONUSMAYA", "BASLAYIN", rg_oled_text_width(line, 1) + 1, dx);
        break;
    case GB_UI_MAX_DURATION:
    case GB_UI_HANG_UP:
        rg_oled_action_screen(count, "AHIZEYI", "KAPATIN", 0, dx);
        break;
    case GB_UI_SAVING: /* KAYDEDILIYOR (12 characters) no longer fits beside the larger count */
        rg_oled_action_screen(count, "LUTFEN", "BEKLEYIN", 0, dx);
        break;
    case GB_UI_SUCCESS:
        rg_oled_action_screen(count, "MESAJINIZ", "KAYDEDILDI", 0, dx); /* 10 characters: no room for '!' */
        break;
    case GB_UI_ERROR:
        draw_error(m);
        break;
    case GB_UI_WIFI:
        draw_wifi(m, count, dx);
        break;
    }
    return true;
}

static void render(const gb_ui_model_t *m)
{
    if (!rg_oled_present()) {
        return;
    }
    xSemaphoreTake(s_draw, portMAX_DELAY);
    if (draw(m)) {
        if (!s_panel_on) {
            rg_oled_display_on(true);
            s_panel_on = true;
        }
        rg_oled_flush();
    } else if (s_panel_on) {
        rg_oled_flush();           /* blank RAM: no stale screen flashes up on the next wake */
        rg_oled_display_on(false); /* panel sleep: charge pump off, ~uA */
        s_panel_on = false;
    }
    xSemaphoreGive(s_draw);
}

static void ui_task(void *arg)
{
    (void)arg;
    gb_ui_model_t m;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_mux);
        m = s_model;
        portEXIT_CRITICAL(&s_mux);
        render(&m);
    }
}

void gb_ui_init(void)
{
    s_draw = xSemaphoreCreateMutex();
    xTaskCreate(ui_task, "gb_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, &s_task);
}

void gb_ui_show(const gb_ui_model_t *model)
{
    portENTER_CRITICAL(&s_mux);
    s_model = *model;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(s_task);
}

void gb_ui_show_sync(const gb_ui_model_t *model)
{
    portENTER_CRITICAL(&s_mux);
    s_model = *model;
    portEXIT_CRITICAL(&s_mux);
    render(model);
}
