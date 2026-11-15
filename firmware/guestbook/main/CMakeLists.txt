idf_component_register(
    SRCS
        "gb_main.c"
        "gb_audio.c"
        "gb_fsm.c"
        "gb_soak.c"
        "gb_ui.c"
    INCLUDE_DIRS "."
    REQUIRES rg esp_timer esp_hw_support esp_system freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
