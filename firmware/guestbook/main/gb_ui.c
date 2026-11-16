#include "gb_ui.h"
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

static void render(const gb_ui_model_t *m)
{
    if (!rg_oled_present()) {
        return;
    }
    xSemaphoreTake(s_draw, portMAX_DELAY);
    if (gb_ui_draw(m)) {
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
