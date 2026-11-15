#include "rg_power.h"
#include "rg_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "rg_power";
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static uint32_t s_users;
static volatile uint32_t s_generation;

static void lock(void)
{
    if (!s_lock) {
        portENTER_CRITICAL(&s_init_mux);
        if (!s_lock) {
            s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
        }
        portEXIT_CRITICAL(&s_init_mux);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

void rg_power_acquire(void)
{
    lock();
    if (s_users++ == 0) {
        gpio_set_level(RG_PIN_PWR_EN, 1);
        s_generation++;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_RG_POWER_SETTLE_MS));
        ESP_LOGD(TAG, "rail on");
    }
    unlock();
}

void rg_power_release(void)
{
    lock();
    if (s_users == 0) {
        ESP_LOGE(TAG, "release without acquire");
    } else if (--s_users == 0) {
        gpio_set_level(RG_PIN_PWR_EN, 0);
        ESP_LOGD(TAG, "rail off");
    }
    unlock();
}

bool rg_power_is_on(void)
{
    return s_users > 0;
}

uint32_t rg_power_generation(void)
{
    return s_generation;
}
