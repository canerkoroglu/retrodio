#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "sdkconfig.h"

#define RG_PIN_I2S_BCLK ((gpio_num_t)CONFIG_RG_PIN_I2S_BCLK)
#define RG_PIN_I2S_WS   ((gpio_num_t)CONFIG_RG_PIN_I2S_WS)
#define RG_PIN_I2S_DIN  ((gpio_num_t)CONFIG_RG_PIN_I2S_DIN)
#define RG_PIN_I2S_DOUT ((gpio_num_t)CONFIG_RG_PIN_I2S_DOUT)
#define RG_PIN_PWR_EN   ((gpio_num_t)CONFIG_RG_PIN_PWR_EN)
#define RG_PIN_SD_CS    ((gpio_num_t)CONFIG_RG_PIN_SD_CS)
#define RG_PIN_SD_SCK   ((gpio_num_t)CONFIG_RG_PIN_SD_SCK)
#define RG_PIN_SD_MOSI  ((gpio_num_t)CONFIG_RG_PIN_SD_MOSI)
#define RG_PIN_SD_MISO  ((gpio_num_t)CONFIG_RG_PIN_SD_MISO)
#define RG_PIN_I2C_SDA  ((gpio_num_t)CONFIG_RG_PIN_I2C_SDA)
#define RG_PIN_I2C_SCL  ((gpio_num_t)CONFIG_RG_PIN_I2C_SCL)
#define RG_PIN_LED      ((gpio_num_t)CONFIG_RG_PIN_LED)
#define RG_PIN_HOOK     ((gpio_num_t)CONFIG_RG_PIN_HOOK)

#define RG_SAMPLE_RATE_HZ   16000
#define RG_BYTES_PER_SECOND (RG_SAMPLE_RATE_HZ * 2)

#define RG_MOUNT_POINT      "/sdcard"
#define RG_DIR_AUDIO        RG_MOUNT_POINT "/audio"
#define RG_DIR_GREETINGS    RG_DIR_AUDIO "/greetings"
#define RG_DIR_RECORDINGS   RG_DIR_AUDIO "/recordings"
#define RG_FILE_GREETING    RG_DIR_GREETINGS "/greeting.wav"
#define RG_FILE_INSTRUCTION RG_DIR_GREETINGS "/instruction.wav"
#define RG_FILE_BEEP        RG_DIR_GREETINGS "/beep.wav"
#define RG_FILE_CONFIG      RG_DIR_AUDIO "/config.txt"
#define RG_FILE_WIFI        RG_DIR_AUDIO "/wifi.txt"
#define RG_FILE_MANIFEST    RG_DIR_AUDIO "/manifest.csv"
#define RG_FILE_LOG         RG_DIR_AUDIO "/log.txt"

/**
 * Put every board pin with a defined idle level into its safe state:
 * peripheral rail off, SD CS released to its pull-up, LED data low.
 * Call first in app_main, before any driver is initialised.
 */
void rg_board_init_safe_state(void);

/** Configure a pin as a plain input with no internal pulls (high-Z). */
void rg_board_pin_float(gpio_num_t pin);
