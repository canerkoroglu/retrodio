idf_component_register(
    SRCS
        "gb_main.c"
        "gb_audio.c"
        "gb_fsm.c"
        "gb_soak.c"
        "gb_ui.c"
        "gb_ui_draw.c"
        "gb_web.c"
    INCLUDE_DIRS "."
    REQUIRES rg esp_timer esp_hw_support esp_system freertos esp_wifi esp_netif esp_event esp_http_server
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
