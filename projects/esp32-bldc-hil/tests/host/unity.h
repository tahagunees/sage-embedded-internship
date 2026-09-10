#pragma once
/* Minimal host assertion adapter, NOT the ESP-IDF Unity library.
 * The exact same portable cases run against real Unity in test_app. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned host_cases;
#define HOST_CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define UNITY_BEGIN() ((void)(host_cases = 0))
#define UNITY_END() ((void)printf("%u portable tests passed\n", host_cases))
#define RUN_TEST(f) do { setUp(); f(); tearDown(); ++host_cases; printf("PASS %s\n", #f); } while (0)
#define TEST_ASSERT_TRUE(x) HOST_CHECK(x)
#define TEST_ASSERT_FALSE(x) HOST_CHECK(!(x))
#define TEST_ASSERT_EQUAL(a,b) HOST_CHECK((a) == (b))
#define TEST_ASSERT_EQUAL_FLOAT(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_UINT16(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_UINT32(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_UINT64(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_INT16(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_INT32(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_HEX8(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_EQUAL_HEX16(a,b) TEST_ASSERT_EQUAL(a,b)
#define TEST_ASSERT_FLOAT_WITHIN(d,a,b) HOST_CHECK(fabs((double)(a) - (double)(b)) <= (double)(d))
