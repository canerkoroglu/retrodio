/*
 * Retro Audio Guestbook - application entry.
 * Every session starts from a deep-sleep wake (fresh boot), so no state leaks between guests.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gb_audio.h"
#include "gb_fsm.h"
#include "gb_soak.h"
#include "gb_ui.h"
#include "rg_board.h"
#include "rg_hook.h"
#include "rg_led.h"
#include "rg_oled.h"
#include "rg_system.h"

static const char *TAG = "guestbook";

void app_main(void)
{
    rg_board_init_safe_state(); /* rail off, SD deselected, LED data low */
    static rg_boot_info_t boot;
    rg_system_init(&boot);
    ESP_LOGI(TAG, "fw %s, boot #%lu, reset %s, crashes %lu%s", boot.version, (unsigned long)boot.boot_count,
             rg_reset_reason_name(boot.reason), (unsigned long)boot.consecutive_crashes,
             boot.safe_mode ? ", SAFE MODE" : "");

    gb_soak_boot(boot.from_deep_sleep);
    rg_hook_init();
    rg_oled_init(boot.from_deep_sleep); /* warm: keep the standby image until the next frame */
    rg_led_init();
    gb_ui_init();

    QueueHandle_t events = xQueueCreate(16, sizeof(gb_audio_event_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gb_audio_init(events));
    gb_fsm_run(&boot, events);
}
