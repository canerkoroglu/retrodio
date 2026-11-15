/*
 * T1: chip info + external bias resistor check.
 * Each pin is driven to one level, released (input, no internal pulls) and read 50 us later.
 * A resistor to the opposite rail pulls the pin over within a few RC time constants;
 * a floating pin keeps its charge. This detects 10k..100k pull resistors without extra hardware.
 */
#include <stdio.h>
#include "hwtest.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "rg_board.h"
#include "rg_hook.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum { BIAS_FLOAT, BIAS_UP, BIAS_DOWN, BIAS_DRIVEN } bias_t;
static const char *const BIAS_NAMES[] = {"floating", "pull-up", "pull-down", "driven/unclear"};

static int release_and_read(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_delay_us(20);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(50);
    return gpio_get_level(pin);
}

static bias_t probe(gpio_num_t pin)
{
    rg_board_pin_float(pin);
    bool up = release_and_read(pin, 0) == 1;
    bool down = release_and_read(pin, 1) == 0;
    rg_board_pin_float(pin);
    if (up && !down) {
        return BIAS_UP;
    }
    if (down && !up) {
        return BIAS_DOWN;
    }
    return up && down ? BIAS_DRIVEN : BIAS_FLOAT;
}

typedef struct {
    gpio_num_t pin;
    const char *signal;
    bias_t expect; /* BIAS_DRIVEN = informational only */
    const char *fix;
} pin_check_t;

tr_t t1_board(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    printf("IDF %s, ESP32-C3 rev v%d.%d, flash %lu MB, free heap %lu B, largest block %u B, reset reason %d\n",
           esp_get_idf_version(), chip.revision / 100, chip.revision % 100, (unsigned long)(flash >> 20),
           (unsigned long)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           esp_reset_reason());

    bool lifted = rg_hook_raw_lifted();
    printf("Hook: handset %s. Run this test with the handset ON the cradle.\n", lifted ? "LIFTED" : "down");

    const pin_check_t checks[] = {
        {RG_PIN_I2S_BCLK, "I2S BCLK", BIAS_DOWN, "fit 100k BCLK->GND"},
        {RG_PIN_I2S_WS, "I2S WS", BIAS_DOWN, "fit 100k WS->GND"},
        {RG_PIN_SD_CS, "SD CS (strap)", BIAS_UP, "fit 100k CS->3V3"},
        {RG_PIN_HOOK, "HOOK", lifted ? BIAS_DOWN : BIAS_UP, "fit 10k HOOK->3V3; check COM->GND, NC->GPIO"},
        {RG_PIN_I2S_DIN, "Mic SD", BIAS_DOWN, "fit 100k mic SD->GND"},
        {RG_PIN_LED, "LED data", BIAS_DOWN, "fit 10k shifter input->GND"},
        {RG_PIN_I2C_SDA, "I2C SDA (strap)", BIAS_UP, "fit 4.7k SDA->3V3"},
        {RG_PIN_I2C_SCL, "I2C SCL (strap)", BIAS_UP, "fit 4.7k SCL->3V3"},
        {RG_PIN_PWR_EN, "PWR_EN", BIAS_DOWN, "fit 100k PWR_EN (N-FET gate)->GND"},
        {RG_PIN_SD_SCK, "SD SCK", BIAS_DRIVEN, NULL},
        {RG_PIN_I2S_DOUT, "Amp DIN", BIAS_DRIVEN, NULL},
        {RG_PIN_SD_MISO, "SD MISO", BIAS_DRIVEN, NULL},
        {RG_PIN_SD_MOSI, "SD MOSI", BIAS_DRIVEN, NULL},
    };

    int failures = 0;
    printf("\n GPIO  signal            measured        expected\n");
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        const pin_check_t *c = &checks[i];
        bias_t got = probe(c->pin);
        bool info = c->expect == BIAS_DRIVEN;
        bool ok = info || got == c->expect;
        failures += !ok;
        printf(" %4d  %-16s  %-14s  %-14s %s%s\n", c->pin, c->signal, BIAS_NAMES[got],
               info ? "(info)" : BIAS_NAMES[c->expect], ok ? "" : "FAIL -> ", ok ? "" : c->fix);
    }

    hw_result(1, failures ? TR_FAIL : TR_PASS, "bias_failures=%d hook=%s flash_mb=%lu", failures,
              lifted ? "lifted" : "down", (unsigned long)(flash >> 20));

    /* Probing re-muxed pins owned by I2C/RMT/SPI drivers; a reset restores every routing cleanly. */
    printf("Rebooting in 3 s to restore peripheral pin routing...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return failures ? TR_FAIL : TR_PASS;
}
