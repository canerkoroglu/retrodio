idf_component_register(
    SRCS
        "rg_audio.c"
        "rg_board.c"
        "rg_catalog.c"
        "rg_catalog_scan.c"
        "rg_crc32.c"
        "rg_err.c"
        "rg_eventlog.c"
        "rg_font5x7.c"
        "rg_hook.c"
        "rg_led.c"
        "rg_mic_conv.c"
        "rg_oled.c"
        "rg_power.c"
        "rg_recorder.c"
        "rg_settings.c"
        "rg_storage.c"
        "rg_system.c"
        "rg_wav.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_sdspi
        esp_driver_spi
        sdmmc
        fatfs
        nvs_flash
        esp_timer
        esp_hw_support
        esp_system
    PRIV_REQUIRES
        vfs
        espcoredump
        esp_app_format
)

# Project code must stay warning-free; third-party and IDF code keeps its own flags.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
