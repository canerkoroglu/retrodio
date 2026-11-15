idf_component_register(
    SRCS
        "hwtest_main.c"
        "hw_util.c"
        "t1_board.c"
        "t2_oled.c"
        "t3_led.c"
        "t4_hook.c"
        "t5_sd.c"
        "t6_mic.c"
        "t7_amp.c"
        "t8_duplex.c"
        "t9_sleep.c"
    INCLUDE_DIRS "."
    REQUIRES rg esp_driver_usb_serial_jtag esp_timer esp_hw_support spi_flash esp_driver_gpio
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
