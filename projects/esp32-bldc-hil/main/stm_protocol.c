#include "stm_protocol.h"
#include <limits.h>
#include <string.h>

static uint16_t get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
/* Avoid implementation-defined unsigned-to-signed conversions. */
static int32_t signed32(uint32_t v)
{ return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v); }
static int16_t signed16(uint16_t v)
{ return v <= INT16_MAX ? (int16_t)v : (int16_t)(-1 - (int32_t)(UINT16_MAX - v)); }
static void put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void add_sat(uint32_t *v, uint32_t n)
{ *v = n > UINT32_MAX - *v ? UINT32_MAX : *v + n; }
uint16_t stm_saturate_u16(uint32_t v)
{ return v > UINT16_MAX ? UINT16_MAX : (uint16_t)v; }
uint16_t stm_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffffU;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (unsigned b = 0; b < 8; ++b)
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1);
    }
    return crc;
}
static void header(uint8_t *p, uint8_t type, uint16_t payload)
{ put16(p, 0xa55aU); p[2] = 1; p[3] = type; put16(p + 4, payload); }
static bool valid(const uint8_t *p, size_t n, uint8_t type, size_t expected)
{
    return n == expected && get16(p) == 0xa55aU && p[2] == 1 && p[3] == type &&
           get16(p + 4) == expected - 8 && get16(p + n - 2) == stm_crc16(p + 2, n - 4);
}
void stm_encode_command(uint8_t p[STM_COMMAND_SIZE], const stm_command_t *c)
{
    header(p, 1, STM_COMMAND_PAYLOAD);
    put32(p + 6, c->sequence); put32(p + 10, c->timestamp_ms);
    put32(p + 14, (uint32_t)c->rpm_x10); put16(p + 18, c->duty_x100);
    p[20] = c->hall; p[21] = c->sector; p[22] = c->flags; p[23] = c->reserved;
    put16(p + 24, stm_crc16(p + 2, 22));
}
bool stm_decode_command(const uint8_t *p, size_t n, stm_command_t *c)
{
    if (!valid(p, n, 1, STM_COMMAND_SIZE)) return false;
    c->sequence = get32(p + 6); c->timestamp_ms = get32(p + 10);
    c->rpm_x10 = signed32(get32(p + 14)); c->duty_x100 = get16(p + 18);
    c->hall = p[20]; c->sector = p[21]; c->flags = p[22]; c->reserved = p[23];
    return c->duty_x100 <= 10000U && c->reserved == 0 && (c->flags & ~3U) == 0;
}
void stm_encode_current(uint8_t p[STM_CURRENT_SIZE], const stm_current_t *c)
{
    header(p, 2, STM_CURRENT_PAYLOAD);
    put32(p + 6, c->received_sequence); put32(p + 10, c->esp_timestamp_ms);
    put16(p + 14, (uint16_t)c->measured_current_ma); put16(p + 16, (uint16_t)c->model_current_ma);
    put32(p + 18, (uint32_t)c->back_emf_mv); put32(p + 22, (uint32_t)c->applied_voltage_mv);
    put16(p + 26, c->status); put16(p + 28, c->rx_error_count);
    put16(p + 30, c->rx_lost_count); put16(p + 32, c->tx_drop_count);
    put16(p + 34, stm_crc16(p + 2, 32));
}
bool stm_decode_current(const uint8_t *p, size_t n, stm_current_t *c)
{
    if (!valid(p, n, 2, STM_CURRENT_SIZE)) return false;
    c->received_sequence = get32(p + 6); c->esp_timestamp_ms = get32(p + 10);
    c->measured_current_ma = signed16(get16(p + 14)); c->model_current_ma = signed16(get16(p + 16));
    c->back_emf_mv = signed32(get32(p + 18)); c->applied_voltage_mv = signed32(get32(p + 22));
    c->status = get16(p + 26); c->rx_error_count = get16(p + 28);
    c->rx_lost_count = get16(p + 30); c->tx_drop_count = get16(p + 32);
    return true;
}
void stm_parser_init(stm_parser_t *p) { memset(p, 0, sizeof(*p)); }
void stm_parser_rx_error(stm_parser_t *p) { p->used = 0; add_sat(&p->errors, 1); }
void stm_parser_overflow(stm_parser_t *p)
{ stm_parser_rx_error(p); p->overflow_seen = true; }
static void discard(stm_parser_t *p)
{ --p->used; memmove(p->bytes, p->bytes + 1, p->used); }
bool stm_parser_byte(stm_parser_t *p, uint8_t byte, uint64_t now_us)
{
    if (p->used >= sizeof(p->bytes)) stm_parser_overflow(p);
    p->bytes[p->used++] = byte;
    while (p->used != 0) {
        if (p->bytes[0] != 0x5aU) { discard(p); continue; }
        if (p->used < 2) return false;
        if (p->bytes[1] != 0xa5U) { discard(p); continue; }
        if (p->used < 6) return false;
        if (p->bytes[2] != 1 || p->bytes[3] != 1 || get16(p->bytes + 4) != STM_COMMAND_PAYLOAD) {
            add_sat(&p->errors, 1); discard(p); continue;
        }
        if (p->used < STM_COMMAND_SIZE) return false;
        if (get16(p->bytes + 24) != stm_crc16(p->bytes + 2, 22)) {
            p->crc_seen = true; add_sat(&p->errors, 1); discard(p); continue;
        }
        stm_command_t c;
        if (!stm_decode_command(p->bytes, STM_COMMAND_SIZE, &c)) {
            add_sat(&p->errors, 1); discard(p); continue;
        }
        p->used = 0;
        if (p->has_command) {
            uint32_t delta = c.sequence - p->latest.sequence;
            if (delta == 0) { add_sat(&p->duplicates, 1); return false; }
            if (delta >= UINT32_C(0x80000000)) { add_sat(&p->old, 1); return false; }
            add_sat(&p->lost, delta - 1);
        }
        p->latest = c; p->received_us = now_us; p->has_command = true;
        return true;
    }
    return false;
}
