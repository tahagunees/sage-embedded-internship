#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { STM_COMMAND_SIZE = 26, STM_CURRENT_SIZE = 36, STM_COMMAND_PAYLOAD = 18,
       STM_CURRENT_PAYLOAD = 28, STM_MAX_PACKET_SIZE = 36 };
enum { STM_ENABLED = 1U, STM_HALL_VALID = 2U };
enum { HIL_HAS_COMMAND = 1U, HIL_VALID_HALL = 2U, HIL_ENABLED = 4U,
       HIL_TIMEOUT = 8U, HIL_CRC_ERROR = 16U, HIL_RX_OVERFLOW = 32U };
typedef struct {
    uint32_t sequence, timestamp_ms;
    int32_t rpm_x10;
    uint16_t duty_x100;
    uint8_t hall, sector, flags, reserved;
} stm_command_t;
typedef struct {
    uint32_t received_sequence, esp_timestamp_ms;
    int16_t measured_current_ma, model_current_ma;
    int32_t back_emf_mv, applied_voltage_mv;
    uint16_t status, rx_error_count, rx_lost_count, tx_drop_count;
} stm_current_t;
typedef struct {
    uint8_t bytes[STM_MAX_PACKET_SIZE];
    size_t used;
    stm_command_t latest;
    uint64_t received_us;
    bool has_command, crc_seen, overflow_seen;
    uint32_t errors, lost, duplicates, old;
} stm_parser_t;

uint16_t stm_crc16(const uint8_t *bytes, size_t size);
void stm_encode_command(uint8_t out[STM_COMMAND_SIZE], const stm_command_t *cmd);
bool stm_decode_command(const uint8_t *bytes, size_t size, stm_command_t *cmd);
void stm_encode_current(uint8_t out[STM_CURRENT_SIZE], const stm_current_t *current);
bool stm_decode_current(const uint8_t *bytes, size_t size, stm_current_t *current);
void stm_parser_init(stm_parser_t *parser);
bool stm_parser_byte(stm_parser_t *parser, uint8_t byte, uint64_t now_us);
void stm_parser_overflow(stm_parser_t *parser);
void stm_parser_rx_error(stm_parser_t *parser);
uint16_t stm_saturate_u16(uint32_t value);
