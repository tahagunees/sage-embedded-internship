idf_component_register(SRCS "test_main.c" "../../tests/test_hil.c"
    "../../main/motor_model.c" "../../main/stm_protocol.c" "../../main/hil_uart.c"
    INCLUDE_DIRS "../../main" REQUIRES unity esp_driver_uart esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
