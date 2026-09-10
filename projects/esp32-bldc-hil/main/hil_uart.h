#pragma once
#include "stm_protocol.h"
#include "esp_err.h"
typedef struct {
    stm_command_t command;
    uint64_t received_us;
    bool has_command, crc_seen, overflow_seen;
    uint32_t errors, lost, duplicates, old, tx_drops;
} hil_snapshot_t;
/* All allocation by IDF drivers occurs in init, before the timer starts. */
esp_err_t hil_uart_init(void);
void hil_uart_snapshot(hil_snapshot_t *snapshot);
bool hil_uart_try_send(const uint8_t packet[STM_CURRENT_SIZE]);
/* Same static queue used by firmware; separable for Unity queue-full tests. */
esp_err_t hil_tx_queue_init(void);
