#include <unity.h>
#include "generic_event_group.h"

#define TEST_BIT_0 (1 << 0)
#define TEST_BIT_1 (1 << 1)

static event_group_handle_t s_event_group;

void setUp(void) {
    s_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(s_event_group);
}

void tearDown(void) {
    event_group_delete(s_event_group);
}

void test_wait_for_any_bit_succeeds(void) {
    // Act
    event_group_set_bits(s_event_group, TEST_BIT_0);
    event_group_bits_t bits = event_group_wait_bits(s_event_group, TEST_BIT_0 | TEST_BIT_1, false, false, 0);

    // Assert
    TEST_ASSERT_EQUAL(TEST_BIT_0, bits & TEST_BIT_0);
}

void test_wait_for_all_bits_succeeds(void) {
    // Act
    event_group_set_bits(s_event_group, TEST_BIT_0 | TEST_BIT_1);
    event_group_bits_t bits = event_group_wait_bits(s_event_group, TEST_BIT_0 | TEST_BIT_1, false, true, 0);

    // Assert
    TEST_ASSERT_EQUAL(TEST_BIT_0 | TEST_BIT_1, bits & (TEST_BIT_0 | TEST_BIT_1));
}

void test_wait_for_all_bits_fails_if_one_bit_is_missing(void) {
    // Act
    event_group_set_bits(s_event_group, TEST_BIT_0);
    event_group_bits_t bits = event_group_wait_bits(s_event_group, TEST_BIT_0 | TEST_BIT_1, false, true, 0);

    // Assert
    TEST_ASSERT_NOT_EQUAL(TEST_BIT_0 | TEST_BIT_1, bits & (TEST_BIT_0 | TEST_BIT_1));
}

void test_clear_on_exit_clears_waited_bits(void) {
    // Arrange
    event_group_set_bits(s_event_group, TEST_BIT_0 | TEST_BIT_1);

    // Act
    event_group_wait_bits(s_event_group, TEST_BIT_0, true, false, 0);
    event_group_bits_t bits_after_wait = event_group_wait_bits(s_event_group, 0xFF, false, false, 0); // Read all current bits

    // Assert
    TEST_ASSERT_EQUAL(0, bits_after_wait & TEST_BIT_0); // Bit 0 should be clear
    TEST_ASSERT_EQUAL(TEST_BIT_1, bits_after_wait & TEST_BIT_1); // Bit 1 should remain set
}

void test_zero_timeout_returns_immediately(void) {
    // Act
    event_group_bits_t bits = event_group_wait_bits(s_event_group, TEST_BIT_0, false, false, 0);

    // Assert
    TEST_ASSERT_EQUAL(0, bits); // No bits are set, should return 0 immediately
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wait_for_any_bit_succeeds);
    RUN_TEST(test_wait_for_all_bits_succeeds);
    RUN_TEST(test_wait_for_all_bits_fails_if_one_bit_is_missing);
    RUN_TEST(test_clear_on_exit_clears_waited_bits);
    RUN_TEST(test_zero_timeout_returns_immediately);
    return UNITY_END();
}


void app_main(void)
{
    main();
}
