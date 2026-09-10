#include "config.h"
#include "hil_uart.h"
#include "motor_model.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <limits.h>

static motor_model_t model;
static StaticTask_t model_control;
static StackType_t model_stack[HIL_TASK_STACK_BYTES / sizeof(StackType_t)];
static TaskHandle_t model_handle;
/* Debugger-visible deadline diagnostic; no output in the 1 kHz path. */
volatile uint32_t hil_missed_ticks;

static bool IRAM_ATTR timer_alarm(gptimer_handle_t timer,
                                  const gptimer_alarm_event_data_t *event, void *context)
{
    (void)timer; (void)event;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)context, &wake);
    return wake == pdTRUE;
}
static void model_task(void *arg)
{
    (void)arg;
    static uint8_t packet[STM_CURRENT_SIZE];
    for (;;) {
        uint32_t ticks = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (ticks == 0) continue;
        if (ticks > 1) {
            uint32_t missed = ticks - 1;
            hil_missed_ticks = missed > UINT32_MAX - hil_missed_ticks ? UINT32_MAX : hil_missed_ticks + missed;
        }
        hil_snapshot_t snap;
        hil_uart_snapshot(&snap);
        uint64_t now = (uint64_t)esp_timer_get_time();
        uint16_t status = motor_step(&model, &snap.command, snap.has_command, snap.received_us, now);
        if (snap.crc_seen) status |= HIL_CRC_ERROR;
        if (snap.overflow_seen) status |= HIL_RX_OVERFLOW;
        stm_current_t reply = {
            .received_sequence = snap.has_command ? snap.command.sequence : 0,
            .esp_timestamp_ms = (uint32_t)(now / 1000U),
            .measured_current_ma = motor_amps_to_ma(model.measured),
            .model_current_ma = motor_amps_to_ma(model.current),
            .back_emf_mv = motor_volts_to_mv(model.back_emf),
            .applied_voltage_mv = motor_volts_to_mv(model.applied),
            .status = status, .rx_error_count = stm_saturate_u16(snap.errors),
            .rx_lost_count = stm_saturate_u16(snap.lost),
            .tx_drop_count = stm_saturate_u16(snap.tx_drops),
        };
        stm_encode_current(packet, &reply);
        (void)hil_uart_try_send(packet);
    }
}
void app_main(void)
{
    motor_params_t params = motor_default_params();
    ESP_ERROR_CHECK(motor_init(&model, &params) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(hil_uart_init());
    model_handle = xTaskCreateStaticPinnedToCore(model_task, "hil_model", sizeof(model_stack), NULL,
        HIL_MODEL_PRIORITY, model_stack, &model_control, 0);
    ESP_ERROR_CHECK(model_handle != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    gptimer_handle_t timer = NULL;
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));
    gptimer_event_callbacks_t callbacks = {.on_alarm = timer_alarm};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, model_handle));
    gptimer_alarm_config_t alarm = {
        .alarm_count = MODEL_PERIOD_US, .reload_count = 0, .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
}
