#include "rg_board.h"

/* Drive a pin at boot without gpio_config(): that would reserve the output path, and the driver
 * that takes the pin later (RMT for the LED) then warns "GPIO n is not usable". */
static void drive(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

void rg_board_pin_float(gpio_num_t pin)
{
    /* Detach the pin from the peripheral that used it (SPI, I2S) and drop its output reservation,
     * so the next driver can take it without a conflict warning. (ESP-IDF's own SD-card removal
     * still logs one harmless "conflict found for GPIO[2]" for CS before this runs.) */
    gpio_reset_pin(pin);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void rg_board_init_safe_state(void)
{
    drive(RG_PIN_PWR_EN, 0);
    rg_board_pin_float(RG_PIN_SD_CS); /* 100k pull-up keeps it high without driving an unpowered module */
    drive(RG_PIN_LED, 0);
}
