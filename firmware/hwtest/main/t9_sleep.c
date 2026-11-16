/*
 * T9: deep-sleep standby and hook wake-up, repeated as many times as you like.
 * State survives deep sleep in RTC memory. Hang up = sleep; lift = wake.
 * Keep the handset lifted for 10 s to finish the test.
 * While asleep, measure the 5 V supply current with a USB power meter (standby budget).
 */
#include <stdio.h>
#include "hwtest.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_hook.h"
#include "rg_led.h"
#include "rg_oled.h"
#include "rg_power.h"
#include "rg_settings.h"
#include "rg_storage.h"

#define T9_MAGIC        0x52475439u
#define FINISH_HOLD_MS  10000
#define SLEEP_STABLE_MS 500

RTC_DATA_ATTR static uint32_t s_magic;
RTC_DATA_ATTR static uint32_t s_wakes, s_false_wakes, s_other_wakes, s_max_ready_ms;
RTC_DATA_ATTR static rg_standby_screen_t s_screen;

bool t9_active(void)
{
    return s_magic == T9_MAGIC;
}

/* Standby screens compared for current draw. MINIMAL lights the fewest pixels; OFF puts the
 * panel to sleep (charge pump off). Pixel shift of 1 px per session limits burn-in. */
static void render_standby(void)
{
    if (!rg_oled_present()) {
        return;
    }
    if (s_screen == RG_STANDBY_OFF) {
        rg_oled_display_on(false);
        return;
    }
    char count[8];
    int shift = (int)(s_wakes % 3) - 1;
    snprintf(count, sizeof(count), "#%03lu", (unsigned long)(s_wakes % 1000));
    rg_oled_clear();
    if (s_screen == RG_STANDBY_FULL) {
        rg_oled_action_screen(count, "MESAJ", NULL, "AHİZEYİ", "KALDIRIN", shift);
    } else {
        rg_oled_action_screen(count, NULL, NULL, NULL, NULL, shift);
    }
    rg_oled_flush();
}

static void sleep_now(void)
{
    render_standby();
    rg_led_off_sync();
    rg_audio_close();
    rg_storage_unmount();
    /* Every user has released the rail: SD, amp, LED and mic are now unpowered. */
    printf("Entering deep sleep (wakes=%lu, rail %s, standby screen %s). Lift the handset to wake.\n",
           (unsigned long)s_wakes, rg_power_is_on() ? "ON - LEAK" : "off",
           s_screen == RG_STANDBY_FULL      ? "full"
           : s_screen == RG_STANDBY_MINIMAL ? "minimal"
                                            : "off");
    rg_hook_arm_wakeup();
    vTaskDelay(pdMS_TO_TICKS(100)); /* let USB flush */
    esp_deep_sleep_start();
}

static void finish(void)
{
    s_magic = 0;
    rg_led_off_sync();
    hw_oled_status("SLEEP TEST", "DONE");
    tr_t r = s_wakes > 0 && s_false_wakes == 0 && s_other_wakes == 0 ? TR_PASS : TR_FAIL;
    hw_result(9, r, "wakes=%lu false_wakes=%lu other_wakes=%lu max_app_ready_ms=%lu standby_screen=%s",
              (unsigned long)s_wakes, (unsigned long)s_false_wakes, (unsigned long)s_other_wakes,
              (unsigned long)s_max_ready_ms,
              s_screen == RG_STANDBY_FULL      ? "full"
              : s_screen == RG_STANDBY_MINIMAL ? "minimal"
                                               : "off");
    printf("Also report the 5 V standby current you measured while it slept (standby_ua=...).\n");
}

/* Handset is lifted: wait for hang-up (-> sleep) or a 10 s hold (-> finish). */
static void lifted_loop(void)
{
    rg_hook_debounce_t d;
    rg_hook_debounce_init(&d, CONFIG_RG_HOOK_DEBOUNCE_MS);
    int64_t lifted_since = esp_timer_get_time();
    for (;;) {
        int64_t now = esp_timer_get_time();
        rg_hook_debounce_update(&d, now);
        if (!d.stable_lifted) {
            bool lifted;
            if (rg_hook_wait_stable(&lifted, SLEEP_STABLE_MS, 2000) && !lifted) {
                sleep_now();
            }
            rg_hook_debounce_init(&d, CONFIG_RG_HOOK_DEBOUNCE_MS);
            lifted_since = esp_timer_get_time();
        } else if (now - lifted_since > FINISH_HOLD_MS * 1000LL) {
            finish();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

tr_t t9_sleep(void)
{
    s_magic = T9_MAGIC;
    s_wakes = s_false_wakes = s_other_wakes = s_max_ready_ms = 0;
    printf("Standby screen to measure: 1 full (spec)  2 minimal (count only)  3 off (panel sleep) ? ");
    int k = hw_key(UINT32_MAX);
    s_screen = k == '2' ? RG_STANDBY_MINIMAL : k == '3' ? RG_STANDBY_OFF : RG_STANDBY_FULL;
    printf("%c\n", k);
    rg_led_init();
    printf("Hang up the handset. The board sleeps; lift to wake, hang up to sleep again.\n"
           "Repeat 20+ times and note the 5 V current while asleep. Hold lifted 10 s to finish.\n"
           "Run the test once per screen variant to compare standby current.\n");
    hw_oled_status("SLEEP TEST", "HANG UP TO START");
    bool lifted = true;
    while (!rg_hook_wait_stable(&lifted, SLEEP_STABLE_MS, 60000) || lifted) {
        if (hw_key_pressed()) {
            s_magic = 0;
            hw_result(9, TR_INFO, "aborted=before_first_sleep");
            return TR_INFO;
        }
    }
    sleep_now();
    return TR_FAIL; /* not reached */
}

void t9_resume(void)
{
    uint32_t ready_ms = (uint32_t)(esp_timer_get_time() / 1000);
    rg_led_init();
    rg_led_set(RG_LED_BLINK_SHORT, RG_RGB_BLUE);
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (!(causes & BIT(ESP_SLEEP_WAKEUP_GPIO))) {
        s_other_wakes++;
    }
    bool lifted = false;
    bool stable = rg_hook_wait_stable(&lifted, CONFIG_RG_HOOK_DEBOUNCE_MS, 1000);
    s_wakes++;
    if (!stable || !lifted) {
        s_false_wakes++;
    }
    s_max_ready_ms = ready_ms > s_max_ready_ms ? ready_ms : s_max_ready_ms;

    char l2[26];
    snprintf(l2, sizeof(l2), "#%lu  APP %lu MS", (unsigned long)s_wakes, (unsigned long)ready_ms);
    hw_oled_status(stable && lifted ? "WOKE: LIFTED" : "FALSE WAKE", l2);
    printf("Wake #%lu: causes=0x%lx hook=%s app_ready=%lu ms (excludes ROM/bootloader)\n", (unsigned long)s_wakes,
           (unsigned long)causes, lifted ? "lifted" : "down", (unsigned long)ready_ms);

    if (!lifted) {
        sleep_now();
    }
    lifted_loop();
}
