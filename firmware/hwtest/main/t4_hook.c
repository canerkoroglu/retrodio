/*
 * T4: hook switch polarity and contact bounce.
 * An any-edge ISR timestamps every transition; a burst of edges separated by <50 ms is one
 * mechanical event, and its first-to-last span is the bounce time.
 */
#include <stdio.h>
#include "hwtest.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rg_board.h"
#include "rg_hook.h"

#define BURST_GAP_US 50000
#define TEST_SECONDS 60

static volatile uint32_t s_edges;
static volatile int64_t s_first_us, s_last_us;

static void IRAM_ATTR hook_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_us > BURST_GAP_US) {
        s_first_us = now;
    }
    s_last_us = now;
    s_edges++;
}

tr_t t4_hook(void)
{
    rg_hook_init();
    gpio_set_intr_type(RG_PIN_HOOK, GPIO_INTR_ANYEDGE);
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        hw_result(4, TR_FAIL, "isr=%s", esp_err_to_name(err));
        return TR_FAIL;
    }
    gpio_isr_handler_add(RG_PIN_HOOK, hook_isr, NULL);

    printf("Lift and replace the handset several times (%d s, any key ends).\n", TEST_SECONDS);
    printf("Initial state: %s\n", rg_hook_raw_lifted() ? "LIFTED" : "DOWN");

    rg_hook_debounce_t deb;
    rg_hook_debounce_init(&deb, CONFIG_RG_HOOK_DEBOUNCE_MS);
    uint32_t lifts = 0, hangups = 0, max_bounce_us = 0, max_edges = 0;
    uint32_t seen_edges = 0;
    int64_t end = esp_timer_get_time() + TEST_SECONDS * 1000000LL;

    while (esp_timer_get_time() < end && !hw_key_pressed()) {
        int64_t now = esp_timer_get_time();
        if (rg_hook_debounce_update(&deb, now)) {
            /* Report once the burst that caused this change has settled. */
            uint32_t edges = s_edges - seen_edges;
            seen_edges = s_edges;
            uint32_t bounce = (uint32_t)(s_last_us - s_first_us);
            if (bounce > max_bounce_us) {
                max_bounce_us = bounce;
            }
            if (edges > max_edges) {
                max_edges = edges;
            }
            if (deb.stable_lifted) {
                lifts++;
            } else {
                hangups++;
            }
            printf("  %-7s edges=%lu bounce=%lu us\n", deb.stable_lifted ? "LIFTED" : "DOWN", (unsigned long)edges,
                   (unsigned long)bounce);
            hw_oled_status("HOOK TEST", deb.stable_lifted ? "LIFTED" : "DOWN");
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    gpio_isr_handler_remove(RG_PIN_HOOK);
    gpio_set_intr_type(RG_PIN_HOOK, GPIO_INTR_DISABLE);

    uint32_t recommended_ms = max_bounce_us * 2 / 1000 + 10;
    if (recommended_ms < 20) {
        recommended_ms = 20;
    }
    bool polarity_ok = lifts > 0 ? hw_ask_yes("Did LIFTED/DOWN match what you physically did?") : false;
    tr_t r = lifts >= 2 && hangups >= 2 && polarity_ok && max_bounce_us / 1000 < CONFIG_RG_HOOK_DEBOUNCE_MS ? TR_PASS
                                                                                                            : TR_FAIL;
    hw_result(4, r,
              "lifts=%lu hangups=%lu max_bounce_us=%lu max_edges=%lu debounce_ms=%d recommended_ms=%lu polarity=%s",
              (unsigned long)lifts, (unsigned long)hangups, (unsigned long)max_bounce_us, (unsigned long)max_edges,
              CONFIG_RG_HOOK_DEBOUNCE_MS, (unsigned long)recommended_ms, polarity_ok ? "ok" : "wrong_or_untested");
    return r;
}
