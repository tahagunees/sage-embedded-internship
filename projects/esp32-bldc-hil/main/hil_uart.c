#include "hil_uart.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <limits.h>

static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static hil_snapshot_t shared;
static stm_parser_t parser;
static QueueHandle_t events, tx_queue;
static StaticQueue_t tx_queue_control;
static uint8_t tx_storage[HIL_TX_QUEUE_DEPTH * STM_CURRENT_SIZE];
static StaticTask_t rx_control, tx_control;
static StackType_t rx_stack[HIL_TASK_STACK_BYTES / sizeof(StackType_t)];
static StackType_t tx_stack[HIL_TASK_STACK_BYTES / sizeof(StackType_t)];

void hil_uart_snapshot(hil_snapshot_t *out)
{
    portENTER_CRITICAL(&snapshot_lock);
    *out = shared;
    portEXIT_CRITICAL(&snapshot_lock);
}
static void publish(void)
{
    portENTER_CRITICAL(&snapshot_lock);
    shared.command = parser.latest;
    shared.received_us = parser.received_us;
    shared.has_command = parser.has_command;
    shared.crc_seen = parser.crc_seen;
    shared.overflow_seen = parser.overflow_seen;
    shared.errors = parser.errors; shared.lost = parser.lost;
    shared.duplicates = parser.duplicates; shared.old = parser.old;
    portEXIT_CRITICAL(&snapshot_lock);
}
static void tx_drop(void)
{
    portENTER_CRITICAL(&snapshot_lock);
    if (shared.tx_drops != UINT32_MAX) ++shared.tx_drops;
    portEXIT_CRITICAL(&snapshot_lock);
}
esp_err_t hil_tx_queue_init(void)
{
    if (tx_queue != NULL) return ESP_ERR_INVALID_STATE;
    tx_queue = xQueueCreateStatic(HIL_TX_QUEUE_DEPTH, STM_CURRENT_SIZE,
                                 tx_storage, &tx_queue_control);
    return tx_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}
bool hil_uart_try_send(const uint8_t packet[STM_CURRENT_SIZE])
{
    if (tx_queue != NULL && xQueueSend(tx_queue, packet, 0) == pdTRUE) return true;
    tx_drop();
    return false;
}
static void rx_task(void *arg)
{
    (void)arg;
    uint8_t chunk[128];
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(events, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            stm_parser_overflow(&parser);
            /* Discard ambiguous buffered data; an old accepted snapshot still ages normally. */
            ESP_ERROR_CHECK(uart_flush_input(UART_NUM_2));
            xQueueReset(events);
            publish();
        } else if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
            stm_parser_rx_error(&parser);
            publish();
        } else if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining > 0) {
                size_t count = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
                int n = uart_read_bytes(UART_NUM_2, chunk, (uint32_t)count, 0);
                if (n <= 0) break;
                remaining -= (size_t)n;
                uint64_t now = (uint64_t)esp_timer_get_time();
                for (int i = 0; i < n; ++i) (void)stm_parser_byte(&parser, chunk[i], now);
                publish();
            }
        }
    }
}
static void tx_task(void *arg)
{
    (void)arg;
    uint8_t packet[STM_CURRENT_SIZE];
    for (;;) {
        if (xQueueReceive(tx_queue, packet, portMAX_DELAY) == pdTRUE) {
            /* TX ring buffer disabled: driver waits here, never in the model task. */
            if (uart_write_bytes(UART_NUM_2, packet, sizeof(packet)) != (int)sizeof(packet)) tx_drop();
        }
    }
}
esp_err_t hil_uart_init(void)
{
    uart_config_t config = {
        .baud_rate = HIL_UART_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_APB,
    };
    esp_err_t err = hil_tx_queue_init();
    if (err != ESP_OK) return err;
    err = uart_param_config(UART_NUM_2, &config);
    if (err != ESP_OK) return err;
    err = uart_set_pin(UART_NUM_2, HIL_UART_TX_GPIO, HIL_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_driver_install(UART_NUM_2, HIL_RX_BUFFER_BYTES, 0,
                              HIL_UART_EVENT_DEPTH, &events, 0);
    if (err != ESP_OK) return err;
    err = uart_set_rx_full_threshold(UART_NUM_2, 32);
    if (err != ESP_OK) return err;
    err = uart_set_rx_timeout(UART_NUM_2, 2);
    if (err != ESP_OK) return err;
    stm_parser_init(&parser);
    if (xTaskCreateStaticPinnedToCore(rx_task, "hil_rx", sizeof(rx_stack), NULL,
            HIL_RX_PRIORITY, rx_stack, &rx_control, 0) == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreateStaticPinnedToCore(tx_task, "hil_tx", sizeof(tx_stack), NULL,
            HIL_TX_PRIORITY, tx_stack, &tx_control, 0) == NULL) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
