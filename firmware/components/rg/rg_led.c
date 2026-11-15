#include "rg_led.h"
#include "rg_board.h"
#include "rg_power.h"
#include "led_strip.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LED_TASK_PERIOD_MS 20
#define LED_TASK_PRIO      2
#define LED_TASK_STACK     2560

static const char *TAG = "rg_led";
static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock; /* guards s_strip + state */
static rg_led_effect_t s_effect = RG_LED_OFF;
static rg_rgb_t s_color;
static int64_t s_effect_start_us;
static uint32_t s_last_rgb = UINT32_MAX; /* forces a refresh */
static uint32_t s_last_gen;
static uint32_t s_max = CONFIG_RG_LED_MAX_BRIGHTNESS;
static bool s_holds_power;

/* 0..255 intensity for the effect at time t (ms since effect start). */
static uint32_t level_at(rg_led_effect_t e, uint32_t t)
{
    switch (e) {
    case RG_LED_SOLID:
        return 255;
    case RG_LED_BLINK_SHORT:
        return (t % 500) < 100 ? 255 : 0;
    case RG_LED_BLINK_FAST:
        return (t % 250) < 125 ? 255 : 0;
    case RG_LED_BREATHE_SLOW:
    case RG_LED_BREATHE: {
        uint32_t period = e == RG_LED_BREATHE_SLOW ? 4000 : 2500;
        uint32_t p = t % period;
        uint32_t tri = p < period / 2 ? p * 510 / period : (period - p) * 510 / period; /* 0..255 */
        uint32_t lvl = tri * tri / 255;                                                 /* perceptual */
        return lvl < 8 ? 8 : lvl;                                                       /* never fully dark */
    }
    case RG_LED_OFF:
    default:
        return 0;
    }
}

static esp_err_t render_locked(void)
{
    if (!rg_power_is_on()) {
        s_last_rgb = UINT32_MAX; /* LED is unpowered: re-send once the rail comes back */
        return ESP_OK;
    }
    uint32_t gen = rg_power_generation();
    if (gen != s_last_gen) {
        s_last_gen = gen;
        s_last_rgb = UINT32_MAX; /* rail was cycled: the LED lost its latched colour */
    }
    uint32_t t = (uint32_t)((esp_timer_get_time() - s_effect_start_us) / 1000);
    uint32_t lvl = level_at(s_effect, t) * s_max / 255;
    uint32_t r = s_color.r * lvl / 255, g = s_color.g * lvl / 255, b = s_color.b * lvl / 255;
    uint32_t packed = (r << 16) | (g << 8) | b;
    if (packed == s_last_rgb) {
        return ESP_OK;
    }
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip); /* blocks until the RMT frame is sent */
    }
    if (err == ESP_OK) {
        s_last_rgb = packed;
    }
    return err;
}

static void led_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        render_locked();
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(LED_TASK_PERIOD_MS));
    }
}

esp_err_t rg_led_init(void)
{
    if (s_strip) {
        return ESP_OK;
    }
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = RG_PIN_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip), TAG, "rmt");
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    s_effect_start_us = esp_timer_get_time();
    BaseType_t ok = xTaskCreate(led_task, "rg_led", LED_TASK_STACK, NULL, LED_TASK_PRIO, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void rg_led_set_max_brightness(uint8_t max)
{
    s_max = max ? max : 1;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_last_rgb = UINT32_MAX;
        xSemaphoreGive(s_lock);
    }
}

void rg_led_set(rg_led_effect_t effect, rg_rgb_t color)
{
    if (!s_strip) {
        return;
    }
    if (effect == RG_LED_OFF) {
        rg_led_off_sync();
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_holds_power) {
        rg_power_acquire(); /* a lit LED keeps the rail on */
        s_holds_power = true;
    }
    if (effect != s_effect || color.r != s_color.r || color.g != s_color.g || color.b != s_color.b) {
        s_effect = effect;
        s_color = color;
        s_effect_start_us = esp_timer_get_time();
        render_locked();
    }
    xSemaphoreGive(s_lock);
}

esp_err_t rg_led_off_sync(void)
{
    if (!s_strip) {
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_effect = RG_LED_OFF;
    esp_err_t err = ESP_OK;
    if (rg_power_is_on()) {
        err = led_strip_clear(s_strip); /* send black before the rail may drop */
    }
    s_last_rgb = err == ESP_OK ? 0 : UINT32_MAX;
    if (s_holds_power) {
        s_holds_power = false;
        rg_power_release();
    }
    xSemaphoreGive(s_lock);
    return err;
}
