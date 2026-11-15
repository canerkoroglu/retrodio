#include "rg_hook.h"
#include "rg_board.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t rg_hook_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RG_PIN_HOOK,
        .mode = GPIO_MODE_INPUT,
        // Internal pull-up only backs up the external 10k while awake; in deep sleep the
        // RTC pad is high-Z and the external resistor alone defines the level.
        .pull_up_en = CONFIG_RG_HOOK_LIFTED_LEVEL == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = CONFIG_RG_HOOK_LIFTED_LEVEL == 1 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

bool rg_hook_raw_lifted(void)
{
    return gpio_get_level(RG_PIN_HOOK) == CONFIG_RG_HOOK_LIFTED_LEVEL;
}

void rg_hook_debounce_reset(rg_hook_debounce_t *d, bool raw_lifted, uint32_t debounce_ms)
{
    d->stable_lifted = raw_lifted;
    d->candidate = raw_lifted;
    d->candidate_since_us = esp_timer_get_time();
    d->debounce_us = debounce_ms * 1000U;
}

void rg_hook_debounce_init(rg_hook_debounce_t *d, uint32_t debounce_ms)
{
    rg_hook_debounce_reset(d, rg_hook_raw_lifted(), debounce_ms);
}

bool rg_hook_debounce_feed(rg_hook_debounce_t *d, bool raw, int64_t now_us)
{
    if (raw != d->candidate) {
        d->candidate = raw;
        d->candidate_since_us = now_us;
        return false;
    }
    if (raw != d->stable_lifted && now_us - d->candidate_since_us >= d->debounce_us) {
        d->stable_lifted = raw;
        return true;
    }
    return false;
}

bool rg_hook_debounce_update(rg_hook_debounce_t *d, int64_t now_us)
{
    return rg_hook_debounce_feed(d, rg_hook_raw_lifted(), now_us);
}

bool rg_hook_wait_stable(bool *lifted, uint32_t stable_ms, uint32_t timeout_ms)
{
    rg_hook_debounce_t d;
    rg_hook_debounce_init(&d, stable_ms);
    int64_t start = esp_timer_get_time();
    int64_t deadline = start + (int64_t)timeout_ms * 1000;
    for (;;) {
        int64_t now = esp_timer_get_time();
        rg_hook_debounce_update(&d, now);
        if (d.candidate == d.stable_lifted && now - d.candidate_since_us >= d.debounce_us) {
            *lifted = d.stable_lifted;
            return true;
        }
        if (now >= deadline) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t rg_hook_arm_wakeup(void)
{
    esp_sleep_gpio_wake_up_mode_t mode =
        CONFIG_RG_HOOK_LIFTED_LEVEL ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW;
    return esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << RG_PIN_HOOK, mode);
}
