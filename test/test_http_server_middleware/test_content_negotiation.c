/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <middleware_content_negotiation.h>

// Test Accept header values for mocking
static char test_accept_value[256] = "";

// Helper function to set test Accept header
void set_test_content_negotiation_header(const char *header_value) {
    if (header_value) {
        strncpy(test_accept_value, header_value, sizeof(test_accept_value) - 1);
        test_accept_value[sizeof(test_accept_value) - 1] = '\0';
    } else {
        test_accept_value[0] = '\0';
    }
}

// Test parsing simple Accept header without quality values
void test_parse_simple_accept_header(void) {
    const char *accept_header = "text/html, application/json";
    httpd_accept_range_t *ranges = httpd_parse_accept_header(accept_header);

    TEST_ASSERT_NOT_NULL(ranges);

    // First range should be text/html with quality 1.0
    TEST_ASSERT_NOT_NULL(ranges->range);
    TEST_ASSERT_EQUAL_STRING("text/html", ranges->range);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, ranges->quality.value);
    TEST_ASSERT_FALSE(ranges->quality.explicit);

    // Second range should be application/json with quality 1.0
    TEST_ASSERT_NOT_NULL(ranges->next);
    TEST_ASSERT_NOT_NULL(ranges->next->range);
    TEST_ASSERT_EQUAL_STRING("application/json", ranges->next->range);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, ranges->next->quality.value);
    TEST_ASSERT_FALSE(ranges->next->quality.explicit);

    // Should be end of list
    TEST_ASSERT_NULL(ranges->next->next);

    httpd_free_accept_ranges(ranges);
}

// Test parsing Accept header with quality values
void test_parse_accept_with_quality(void) {
    const char *accept_header = "text/html;q=0.8, application/json;q=0.9";
    httpd_accept_range_t *ranges = httpd_parse_accept_header(accept_header);

    TEST_ASSERT_NOT_NULL(ranges);

    // First range should be text/html with quality 0.8
    TEST_ASSERT_NOT_NULL(ranges->range);
    TEST_ASSERT_EQUAL_STRING("text/html", ranges->range);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, ranges->quality.value);
    TEST_ASSERT_TRUE(ranges->quality.explicit);

    // Second range should be application/json with quality 0.9
    TEST_ASSERT_NOT_NULL(ranges->next);
    TEST_ASSERT_NOT_NULL(ranges->next->range);
    TEST_ASSERT_EQUAL_STRING("application/json", ranges->next->range);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, ranges->next->quality.value);
    TEST_ASSERT_TRUE(ranges->next->quality.explicit);

    httpd_free_accept_ranges(ranges);
}

// Test parsing empty Accept header
void test_parse_empty_accept_header(void) {
    httpd_accept_range_t *ranges = httpd_parse_accept_header("");
    TEST_ASSERT_NULL(ranges);

    ranges = httpd_parse_accept_header(NULL);
    TEST_ASSERT_NULL(ranges);
}

// Test parsing Accept header with extra whitespace
void test_parse_accept_with_whitespace(void) {
    const char *accept_header = "  text/html  ,   application/json   ";
    httpd_accept_range_t *ranges = httpd_parse_accept_header(accept_header);

    TEST_ASSERT_NOT_NULL(ranges);

    TEST_ASSERT_NOT_NULL(ranges->range);
    TEST_ASSERT_EQUAL_STRING("text/html", ranges->range);

    TEST_ASSERT_NOT_NULL(ranges->next);
    TEST_ASSERT_NOT_NULL(ranges->next->range);
    TEST_ASSERT_EQUAL_STRING("application/json", ranges->next->range);

    httpd_free_accept_ranges(ranges);
}

// Test parsing malformed Accept headers - should reject invalid quality values
void test_parse_accept_malformed_headers(void) {
    // Test malformed quality values
    httpd_accept_range_t *ranges;

    // Invalid quality value: non-numeric
    ranges = httpd_parse_accept_header("type;q=abc");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    // Invalid quality value: out of range high
    ranges = httpd_parse_accept_header("text/html;q=1.5");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    // Invalid quality value: out of range low
    ranges = httpd_parse_accept_header("text/html;q=-0.1");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    // Empty ranges
    ranges = httpd_parse_accept_header(";");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    ranges = httpd_parse_accept_header(",,");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    // Missing quality value after =
    ranges = httpd_parse_accept_header("text/html;q=");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header

    // Empty media type
    ranges = httpd_parse_accept_header(";q=1");
    TEST_ASSERT_NULL(ranges);  // Should reject malformed header
}

// Test parsing partially valid Accept headers - valid parts should be parsed
void test_parse_accept_partially_malformed(void) {
    // Mix of valid and invalid ranges - should reject entire header if any part invalid
    httpd_accept_range_t *ranges;

    ranges = httpd_parse_accept_header("text/html;q=0.8, invalid;q=abc, application/json;q=0.5");
    TEST_ASSERT_NULL(ranges);  // Should reject due to invalid part
}

// Initialize content negotiation tests
void test_content_negotiation_init(void) {
    // Set up any test initialization if needed
    set_test_content_negotiation_header(NULL);
}

// Main test function
int run_test_content_negotiation(void) {
    UnitySetTestFile(__FILE__);
    test_content_negotiation_init();

    // Run all content negotiation tests
    RUN_TEST(test_parse_simple_accept_header);
    RUN_TEST(test_parse_accept_with_quality);
    RUN_TEST(test_parse_empty_accept_header);
    RUN_TEST(test_parse_accept_with_whitespace);
    RUN_TEST(test_parse_accept_malformed_headers);
    RUN_TEST(test_parse_accept_partially_malformed);
    return 0;
}
