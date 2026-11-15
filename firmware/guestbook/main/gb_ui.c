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

static void total_text(char *buf, size_t len, const gb_ui_model_t *m)
{
    if (m->total_known) {
        snprintf(buf, len, "TOPLAM: %03lu", (unsigned long)m->total);
    } else {
        snprintf(buf, len, "TOPLAM: ---");
    }
}

static void draw_error(const gb_ui_model_t *m)
{
    static const char *const TITLE[] = {"KAYIT HATASI", "KART HATASI", "KART DOLU", "SERVIS GEREKLI"};
    static const char *const HINT[] = {"TEKRAR DENEYIN", "KARTI KONTROL EDIN", "KARTI DEGISTIRIN",
                                       "CIHAZI YENIDEN BASLATIN"};
    center(4, TITLE[m->error], 1, 0);
    center(18, HINT[m->error], 1, 0);
}

/* Returns false if the panel should be switched off instead (standby "off"). */
static bool draw(const gb_ui_model_t *m)
{
    char line[26];
    int dx = (int)(m->shift % 3) - 1; /* 1 px burn-in shift per session on static screens */
    rg_oled_clear();
    switch (m->screen) {
    case GB_UI_STANDBY:
        if (m->standby == RG_STANDBY_OFF) {
            return false;
        }
        total_text(line, sizeof(line), m);
        if (m->standby == RG_STANDBY_FULL) {
            center(0, "AUDIO GUESTBOOK", 1, dx);
            center(12, line, 1, dx);
            center(24, m->low_space ? "KART DOLMAK UZERE" : "AHIZEYI KALDIRIN", 1, dx);
        } else {
            center(12 + dx, line, 1, dx);
            if (m->low_space) {
                center(24, "KART DOLMAK UZERE", 1, dx);
            }
        }
        break;
    case GB_UI_WAKING:
        center(12, "HAZIRLANIYOR...", 1, 0);
        break;
    case GB_UI_GREETING:
        center(9, "HOS GELDINIZ", 2, 0);
        break;
    case GB_UI_INSTRUCTION:
        center(6, "SINYAL SESINDEN", 1, 0);
        center(18, "SONRA KONUSUN", 1, 0);
        break;
    case GB_UI_RECORDING:
        center(0, "MESAJINIZI BIRAKIN", 1, 0);
        snprintf(line, sizeof(line), "%02lu:%02lu", (unsigned long)(m->seconds_left / 60),
                 (unsigned long)(m->seconds_left % 60));
        center(13, line, 2, 0);
        break;
    case GB_UI_MAX_DURATION:
        center(0, "SURE DOLDU", 1, 0);
        center(12, "LUTFEN AHIZEYI", 1, 0);
        center(24, "YERINE KOYUN", 1, 0);
        break;
    case GB_UI_SAVING:
        center(12, "KAYDEDILIYOR...", 1, 0);
        break;
    case GB_UI_SUCCESS:
        total_text(line, sizeof(line), m);
        center(4, "KAYIT BASARILI", 1, 0);
        center(18, line, 1, 0);
        break;
    case GB_UI_ERROR:
        draw_error(m);
        break;
    case GB_UI_HANG_UP:
        center(6, "LUTFEN AHIZEYI", 1, 0);
        center(18, "YERINE KOYUN", 1, 0);
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
