#include "rg_board.h"

static void drive(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, level);
}

void rg_board_pin_float(gpio_num_t pin)
{
    /* Detach the pin from the peripheral that used it (SPI CS, I2S) and drop its output
     * reservation; without this gpio_config() warns "conflict found" for a released driver pin. */
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
