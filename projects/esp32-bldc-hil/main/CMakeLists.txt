idf_component_register(SRCS "main.c" "motor_model.c" "stm_protocol.c" "hil_uart.c"
    INCLUDE_DIRS "." REQUIRES esp_driver_uart esp_driver_gptimer esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
