#include "unity.h"
#include "motor_model.h"
#include "stm_protocol.h"
#include "config.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#ifndef HIL_HOST_TEST
#include "hil_uart.h"
#endif

static motor_model_t m;
static stm_command_t c;
static stm_parser_t parser;
void setUp(void)
{
    motor_params_t p = motor_default_params();
    TEST_ASSERT_TRUE(motor_init(&m, &p));
    c = (stm_command_t){.sequence = 1, .duty_x100 = 4025, .hall = 1, .sector = 1, .flags = 3};
    stm_parser_init(&parser);
}
void tearDown(void) {}
static uint16_t step(void) { return motor_step(&m, &c, true, 1000, 1000); }
static bool feed(const uint8_t *bytes, size_t n, uint64_t now)
{
    bool accepted = false;
    for (size_t i = 0; i < n; ++i) accepted |= stm_parser_byte(&parser, bytes[i], now);
    return accepted;
}
static bool command(uint32_t sequence, uint64_t now)
{
    uint8_t bytes[STM_COMMAND_SIZE];
    c.sequence = sequence;
    stm_encode_command(bytes, &c);
    return feed(bytes, sizeof(bytes), now);
}
static void test_startup(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0, m.current);
    TEST_ASSERT_EQUAL_FLOAT(0, m.applied);
    uint16_t s = motor_step(&m, &c, false, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(HIL_TIMEOUT, s);
    TEST_ASSERT_EQUAL_FLOAT(0, m.current);
}
static void test_zero_rpm(void)
{
    step();
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 9.66f / 30.6f, m.current);
    TEST_ASSERT_EQUAL_FLOAT(0, m.back_emf);
    for (int i = 0; i < 1000; ++i) step();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 16.1f, m.current);
}
static void test_rpm_1500(void)
{
    c.rpm_x10 = 15000;
    for (int i = 0; i < 1000; ++i) step();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 7.8539816f, m.back_emf);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, (9.66f - 7.8539816f) / 0.6f, m.current);
}
static void test_negative_rpm(void)
{
    c.rpm_x10 = -15000; step(); float neg = m.current;
    m.current = 0; c.rpm_x10 = 15000; step();
    TEST_ASSERT_EQUAL_FLOAT(neg, m.current);
    c.rpm_x10 = INT32_MIN; step();
    TEST_ASSERT_TRUE(isfinite(m.back_emf)); TEST_ASSERT_EQUAL_FLOAT(0, m.current);
}
static void test_duty_limits(void)
{
    c.duty_x100 = 0; step(); TEST_ASSERT_EQUAL_FLOAT(0, m.current);
    c.duty_x100 = 10000;
    for (int i = 0; i < 1000; ++i) step();
    TEST_ASSERT_EQUAL_FLOAT(24, m.applied); TEST_ASSERT_EQUAL_FLOAT(20, m.current);
    c.rpm_x10 = 100000; step(); TEST_ASSERT_TRUE(m.current < 20);
}
static void check_decay(void)
{
    m.current = 3; step();
    TEST_ASSERT_EQUAL_FLOAT(0, m.applied);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.0f / 1.02f, m.current);
}
static void test_hall_invalid(void) { c.flags = STM_ENABLED; check_decay(); }
static void test_sector_invalid(void)
{
    c.sector = 0; check_decay(); c.sector = 7; check_decay();
}
static void test_disabled(void) { c.flags = STM_HALL_VALID; check_decay(); }
static void test_timeout(void)
{
    TEST_ASSERT_TRUE(motor_step(&m, &c, true, 10000, 14999) & HIL_HAS_COMMAND);
    m.current = 3;
    uint16_t s = motor_step(&m, &c, true, 10000, 15000);
    TEST_ASSERT_TRUE(s & HIL_TIMEOUT); TEST_ASSERT_FALSE(s & HIL_HAS_COMMAND);
    TEST_ASSERT_EQUAL_FLOAT(0, m.applied);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.0f / 1.02f, m.current);
    TEST_ASSERT_TRUE(motor_step(&m, &c, true, UINT64_C(4294967296000), UINT64_C(4294967301000)) & HIL_TIMEOUT);
}
static void test_long_decay(void)
{
    m.current = 3; c.flags = 0;
    for (int i = 0; i < 1000; ++i) step();
    TEST_ASSERT_TRUE(m.current >= 0 && m.current < 0.0001f);
}
static void test_sensor_filter(void)
{
    step(); TEST_ASSERT_FLOAT_WITHIN(0.00001f, m.current / 3, m.measured);
    float previous = m.measured;
    step(); TEST_ASSERT_FLOAT_WITHIN(0.00001f, previous + (m.current - previous) / 3, m.measured);
    motor_params_t p = motor_default_params(); p.sensor_tau = 0; p.offset = 0.5f;
    TEST_ASSERT_TRUE(motor_init(&m, &p)); step();
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, m.current + 0.5f, m.measured);
}
static void test_noise(void)
{
    motor_params_t p = motor_default_params(); p.sensor_tau = 0; p.noise = 0.1f;
    TEST_ASSERT_TRUE(motor_init(&m, &p));
    for (int i = 0; i < 100; ++i) { step(); TEST_ASSERT_TRUE(fabsf(m.measured - m.current) <= 0.10001f); }
}
static void test_bad_parameters(void)
{
    motor_params_t p = motor_default_params(); p.r = 0; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.l = -1; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.ke = NAN; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.vbus = INFINITY; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.sensor_tau = -1; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.max_current = 40; TEST_ASSERT_FALSE(motor_init(&m, &p));
    p = motor_default_params(); p.offset = NAN; TEST_ASSERT_FALSE(motor_init(&m, &p));
}
static void test_crc_vector(void)
{ TEST_ASSERT_EQUAL_HEX16(0x29b1, stm_crc16((const uint8_t *)"123456789", 9)); }
static void test_wire_roundtrip(void)
{
    uint8_t bytes[STM_COMMAND_SIZE]; stm_command_t decoded;
    c.sequence = 0x12345678; c.rpm_x10 = -15000;
    stm_encode_command(bytes, &c);
    TEST_ASSERT_EQUAL_HEX8(0x5a, bytes[0]); TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x78, bytes[6]); TEST_ASSERT_EQUAL_HEX8(0x12, bytes[9]);
    TEST_ASSERT_EQUAL_UINT16(18, bytes[4]);
    TEST_ASSERT_TRUE(stm_decode_command(bytes, sizeof(bytes), &decoded));
    TEST_ASSERT_EQUAL_INT32(-15000, decoded.rpm_x10); TEST_ASSERT_EQUAL_UINT16(4025, decoded.duty_x100);
    stm_current_t reply = {.received_sequence = 0xfedcba98, .esp_timestamp_ms = 42,
        .measured_current_ma = -123, .model_current_ma = 20000, .back_emf_mv = 7854,
        .applied_voltage_mv = 9660, .status = 63, .rx_error_count = 5, .rx_lost_count = 7, .tx_drop_count = 9};
    uint8_t out[STM_CURRENT_SIZE]; stm_current_t result;
    stm_encode_current(out, &reply);
    TEST_ASSERT_TRUE(stm_decode_current(out, sizeof(out), &result));
    TEST_ASSERT_EQUAL_INT16(-123, result.measured_current_ma);
    TEST_ASSERT_EQUAL_UINT32(reply.received_sequence, result.received_sequence);
    TEST_ASSERT_EQUAL_INT32(7854, result.back_emf_mv); TEST_ASSERT_EQUAL_UINT16(9, result.tx_drop_count);
    out[20] ^= 1; TEST_ASSERT_FALSE(stm_decode_current(out, sizeof(out), &result));
}
static void test_crc_bad_and_recovery(void)
{
    uint8_t bytes[STM_COMMAND_SIZE]; stm_encode_command(bytes, &c); bytes[10] ^= 1;
    TEST_ASSERT_FALSE(feed(bytes, sizeof(bytes), 100));
    TEST_ASSERT_TRUE(parser.crc_seen); TEST_ASSERT_FALSE(parser.has_command);
    TEST_ASSERT_TRUE(command(1, 200));
    TEST_ASSERT_FALSE(feed(bytes, sizeof(bytes), 300));
    TEST_ASSERT_TRUE(parser.received_us == UINT64_C(200));
    TEST_ASSERT_TRUE(motor_step(&m, &parser.latest, parser.has_command, parser.received_us, 5199) & HIL_HAS_COMMAND);
    TEST_ASSERT_TRUE(motor_step(&m, &parser.latest, parser.has_command, parser.received_us, 5200) & HIL_TIMEOUT);
}
static void test_incomplete(void)
{
    uint8_t bytes[STM_COMMAND_SIZE]; stm_encode_command(bytes, &c);
    TEST_ASSERT_FALSE(feed(bytes, 25, 10)); TEST_ASSERT_FALSE(parser.has_command);
    TEST_ASSERT_TRUE(feed(bytes + 25, 1, 20));
    TEST_ASSERT_TRUE(parser.received_us == UINT64_C(20));
}
static void test_resync_and_lengths(void)
{
    uint8_t bytes[STM_COMMAND_SIZE]; stm_encode_command(bytes, &c);
    TEST_ASSERT_FALSE(feed(bytes, 13, 10));
    TEST_ASSERT_TRUE(feed(bytes, sizeof(bytes), 20));
    bytes[4] = 0xff; bytes[5] = 0xff;
    TEST_ASSERT_FALSE(feed(bytes, sizeof(bytes), 30));
    TEST_ASSERT_TRUE(command(2, 40));
}
static void test_overflow(void)
{
    uint8_t bytes[STM_COMMAND_SIZE]; stm_encode_command(bytes, &c);
    feed(bytes, 12, 10); stm_parser_overflow(&parser);
    TEST_ASSERT_EQUAL_UINT32(0, parser.used); TEST_ASSERT_TRUE(parser.overflow_seen);
    TEST_ASSERT_EQUAL_UINT32(1, parser.errors); TEST_ASSERT_TRUE(command(1, 20));
    stm_parser_overflow(&parser); TEST_ASSERT_TRUE(parser.has_command);
    TEST_ASSERT_TRUE(parser.received_us == UINT64_C(20));
}
static void test_duplicate_old(void)
{
    TEST_ASSERT_TRUE(command(10, 100)); TEST_ASSERT_FALSE(command(10, 200));
    TEST_ASSERT_FALSE(command(9, 300)); TEST_ASSERT_EQUAL_UINT32(1, parser.duplicates);
    TEST_ASSERT_EQUAL_UINT32(1, parser.old); TEST_ASSERT_TRUE(parser.received_us == UINT64_C(100));
}
static void test_lost_wrap(void)
{
    TEST_ASSERT_TRUE(command(UINT32_MAX - 1, 10)); TEST_ASSERT_TRUE(command(UINT32_MAX, 20));
    TEST_ASSERT_TRUE(command(0, 30)); TEST_ASSERT_EQUAL_UINT32(0, parser.lost);
    TEST_ASSERT_TRUE(command(4, 40)); TEST_ASSERT_EQUAL_UINT32(3, parser.lost);
    TEST_ASSERT_FALSE(command(UINT32_MAX, 50));
    TEST_ASSERT_FALSE(command(0x80000004U, 60));
}
static void test_bad_semantics(void)
{
    c.duty_x100 = 10001; TEST_ASSERT_FALSE(command(1, 10));
    c.duty_x100 = 10000; c.reserved = 1; TEST_ASSERT_FALSE(command(1, 20));
    c.reserved = 0; c.flags = 4; TEST_ASSERT_FALSE(command(1, 30));
    c.flags = 0; TEST_ASSERT_TRUE(command(1, 40));
}
static void test_parser_noise_bounds(void)
{
    uint32_t random = 123;
    for (unsigned i = 0; i < 100000; ++i) {
        random = random * 1664525U + 1013904223U;
        stm_parser_byte(&parser, (uint8_t)(random >> 24), i);
        TEST_ASSERT_TRUE(parser.used < STM_MAX_PACKET_SIZE);
    }
    TEST_ASSERT_TRUE(command(1, 100001));
}
static void test_conversions(void)
{
    TEST_ASSERT_EQUAL_INT16(32767, motor_amps_to_ma(100));
    TEST_ASSERT_EQUAL_INT16(-32768, motor_amps_to_ma(-100));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, motor_volts_to_mv(1e20f));
    TEST_ASSERT_EQUAL_INT16(0, motor_amps_to_ma(NAN));
    TEST_ASSERT_EQUAL_UINT16(65535, stm_saturate_u16(UINT32_MAX));
}
/* Test-only controller represents STM32; production ESP code has no PI. */
static void test_external_pi_reference_step(void)
{
    float integral = 0;
    for (int i = 0; i < 3000; ++i) {
        float reference = i < 100 ? 0 : 3;
        float error = reference - m.measured;
        integral += 15.0f * error * 0.001f;
        float volts = fminf(24, fmaxf(0, 2.0f * error + integral));
        c.duty_x100 = (uint16_t)lroundf(volts / 24.0f * 10000);
        step();
        if (i < 100) TEST_ASSERT_EQUAL_FLOAT(0, m.current);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3, m.measured);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 7.5f, (float)c.duty_x100 / 100);
}
#ifndef HIL_HOST_TEST
static void test_tx_queue_full(void)
{
    uint8_t packet[STM_CURRENT_SIZE] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, hil_tx_queue_init());
    for (int i = 0; i < HIL_TX_QUEUE_DEPTH; ++i) TEST_ASSERT_TRUE(hil_uart_try_send(packet));
    TEST_ASSERT_FALSE(hil_uart_try_send(packet));
    hil_snapshot_t s; hil_uart_snapshot(&s);
    TEST_ASSERT_EQUAL_UINT32(1, s.tx_drops);
}
#endif
void run_hil_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_startup); RUN_TEST(test_zero_rpm); RUN_TEST(test_rpm_1500);
    RUN_TEST(test_negative_rpm); RUN_TEST(test_duty_limits); RUN_TEST(test_hall_invalid);
    RUN_TEST(test_sector_invalid); RUN_TEST(test_disabled); RUN_TEST(test_timeout);
    RUN_TEST(test_long_decay); RUN_TEST(test_sensor_filter); RUN_TEST(test_noise);
    RUN_TEST(test_bad_parameters); RUN_TEST(test_crc_vector); RUN_TEST(test_wire_roundtrip);
    RUN_TEST(test_crc_bad_and_recovery); RUN_TEST(test_incomplete); RUN_TEST(test_resync_and_lengths);
    RUN_TEST(test_overflow); RUN_TEST(test_duplicate_old); RUN_TEST(test_lost_wrap);
    RUN_TEST(test_bad_semantics); RUN_TEST(test_parser_noise_bounds); RUN_TEST(test_conversions);
    RUN_TEST(test_external_pi_reference_step);
#ifndef HIL_HOST_TEST
    RUN_TEST(test_tx_queue_full);
#endif
    UNITY_END();
}
