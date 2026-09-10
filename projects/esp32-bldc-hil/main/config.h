#pragma once

#define MODEL_VBUS_V                  24.0f
#define MODEL_R_EQ_OHM                 0.60f
#define MODEL_L_EQ_H                   0.030f
#define MODEL_KE_V_PER_RAD_S           0.050f
#define MODEL_MAX_CURRENT_A           20.0f
#define MODEL_CURRENT_OFFSET_A         0.0f
#define MODEL_CURRENT_NOISE_A          0.0f
#define MODEL_SENSOR_FILTER_TAU_S      0.002f
#define MODEL_COMMAND_TIMEOUT_MS       5U
#define MODEL_PERIOD_US             1000U

#define HIL_UART_BAUD             4000000
#define HIL_UART_RX_GPIO               16
#define HIL_UART_TX_GPIO               17
#define HIL_RX_BUFFER_BYTES          2048
#define HIL_UART_EVENT_DEPTH           32
#define HIL_TX_QUEUE_DEPTH              4
#define HIL_MODEL_PRIORITY             20
#define HIL_RX_PRIORITY                19
#define HIL_TX_PRIORITY                18
#define HIL_TASK_STACK_BYTES         4096
