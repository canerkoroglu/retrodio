/* T3: every status LED state from the spec, at the configured brightness cap. */
#include <stdio.h>
#include "hwtest.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rg_led.h"

typedef struct {
    const char *state;
    rg_led_effect_t effect;
    rg_rgb_t color;
} led_step_t;

tr_t t3_led(void)
{
    esp_err_t err = rg_led_init();
    if (err != ESP_OK) {
        hw_result(3, TR_FAIL, "init=%s", esp_err_to_name(err));
        return TR_FAIL;
    }
    const led_step_t steps[] = {
        {"WAKING      blue, short blink", RG_LED_BLINK_SHORT, RG_RGB_BLUE},
        {"GREETING    yellow, slow breathing", RG_LED_BREATHE_SLOW, RG_RGB_YELLOW},
        {"RECORDING   green, breathing", RG_LED_BREATHE, RG_RGB_GREEN},
        {"SAVING      cyan, fast blink", RG_LED_BLINK_FAST, RG_RGB_CYAN},
        {"SUCCESS     green, solid", RG_LED_SOLID, RG_RGB_GREEN},
        {"ERROR       red, fast blink", RG_LED_BLINK_FAST, RG_RGB_RED},
        {"WHITE       solid (colour balance check)", RG_LED_SOLID, RG_RGB_WHITE},
    };
    printf("Brightness cap: %d/255\n", CONFIG_RG_LED_MAX_BRIGHTNESS);
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        printf("  %s\n", steps[i].state);
        hw_oled_status("LED TEST", steps[i].state);
        rg_led_set(steps[i].effect, steps[i].color);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    err = rg_led_off_sync();
    printf("  OFF\n");
    bool colors_ok = hw_ask_yes("Did each colour/effect match the description?");
    bool off_ok = hw_ask_yes("Is the LED completely dark now?");
    bool comfy = hw_ask_yes("Was the brightness comfortable (not glaring) indoors?");
    tr_t r = colors_ok && off_ok && err == ESP_OK ? TR_PASS : TR_FAIL;
    hw_result(3, r, "colors=%s off=%s brightness_ok=%s cap=%d", colors_ok ? "yes" : "no", off_ok ? "yes" : "no",
              comfy ? "yes" : "no", CONFIG_RG_LED_MAX_BRIGHTNESS);
    return r;
}
