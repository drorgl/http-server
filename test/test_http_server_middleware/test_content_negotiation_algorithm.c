#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <middleware_content_negotiation.h>

#include "middleware_content_negotiation.h"

/**
 * @brief Test RFC 9110 Content Negotiation Algorithm
 *
 * This test suite covers the core scoring algorithm and media type matching
 * logic per RFC 9110 Section 12.5.1 Content Negotiation.
 */

// Test RFC 9110 scoring algorithm
void test_rfc9110_exact_match_has_highest_priority(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "text/plain", "application/json"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 3
    };

    // Setup client accept ranges: exact match with lower quality vs wildcard with higher quality
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html;q=0.8, */*;q=0.9");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("text/html", result.selected_media_type);
    TEST_ASSERT_TRUE(result.vary_header_needed);
    TEST_ASSERT_EQUAL_STRING("Accept", result.vary_header_value);
    TEST_ASSERT_EQUAL_FLOAT(800.0f, result.media_type_score);  // 1000 * 0.8

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_specificity_precedence_type_wildcard(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "application/json", "text/plain"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 3
    };

    // Test: type/* wildcard beats */* but loses to exact match
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/*;q=0.8, */*;q=0.9, text/html;q=0.7");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("text/html", result.selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(700.0f, result.media_type_score);  // 1000 * 0.7 (highest specificity)

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_quality_precedence_within_same_specificity(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "text/plain"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 2
    };

    // Test: Higher quality wins within same specificity level
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html;q=0.8, text/plain;q=0.9");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("text/plain", result.selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, result.media_type_score);  // 1000 * 0.9

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_universal_wildcard_matches_everything(void) {
    // Setup server capabilities
    char *server_types[] = {"application/json", "image/png"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 2
    };

    // Test: */* should match anything and pick first available (no quality ordering)
    httpd_accept_range_t *ranges = httpd_parse_accept_header("*/*;q=0.5");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("application/json", result.selected_media_type);  // First in array
    TEST_ASSERT_EQUAL_FLOAT(5.0f, result.media_type_score);  // 10 * 0.5

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_type_wildcard_matching(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "text/plain", "application/json"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 3
    };

    // Test: text/* should match both text types, highest quality wins
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/*;q=0.8");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    // Should pick the first matching type in server capabilities: text/html
    TEST_ASSERT_EQUAL_STRING("text/html", result.selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, result.media_type_score);  // 100 * 0.8

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_no_accept_header_fallback(void) {
    // Setup server capabilities
    char *server_types[] = {"application/json", "text/html"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 2
    };

    // Test: No Accept header should fall back to first server capability
    httpd_accept_range_t *ranges = NULL;  // No Accept header

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("application/json", result.selected_media_type);  // First in array
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result.media_type_score);  // Indicates fallback
    TEST_ASSERT_FALSE(result.vary_header_needed);  // No negotiation occurred

    httpd_free_negotiation_result(&result);
}

void test_rfc9110_no_matching_server_capabilities_fallback(void) {
    // Setup server capabilities (image types only)
    char *server_types[] = {"image/png", "image/jpeg"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 2
    };

    // Test: Client wants text types, but server only has images - fallback
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html;q=0.9, text/plain;q=0.8");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("image/png", result.selected_media_type);  // First fallback
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result.media_type_score);  // Indicates fallback
    TEST_ASSERT_FALSE(result.vary_header_needed);  // No negotiation occurred

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_unacceptable_quality_values(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "text/plain", "application/json"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 3
    };

    // Test: q=0 should be skipped as unacceptable (RFC 9110 Section 12.4.2)
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html;q=0, text/plain;q=0.8, application/json;q=0.5");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("text/plain", result.selected_media_type);  // q=0 skipped
    TEST_ASSERT_EQUAL_FLOAT(800.0f, result.media_type_score);

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_edge_case_empty_server_capabilities(void) {
    // Setup empty server capabilities
    httpd_content_capabilities_t capabilities = {
        .media_types = NULL,
        .media_type_count = 0
    };

    // Test: No server capabilities available
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NULL(result.selected_media_type);  // No selection possible
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, result.media_type_score);

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

void test_rfc9110_complex_quality_precedence(void) {
    // Setup server capabilities
    char *server_types[] = {"text/html", "application/json", "text/plain"};
    httpd_content_capabilities_t capabilities = {
        .media_types = server_types,
        .media_type_count = 3
    };

    // Complex precedence test matching RFC 9110 documentation example
    httpd_accept_range_t *ranges = httpd_parse_accept_header("text/html;q=0.8, application/json;q=0.9, text/*;q=0.5, */*;q=0.1");

    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("application/json", result.selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, result.media_type_score);  // 1000 * 0.9 (wins over text/html)

    httpd_free_accept_ranges(ranges);
    httpd_free_negotiation_result(&result);
}

/**
 * @brief Initialize content negotiation algorithm tests
 */
void test_content_negotiation_algorithm_init(void) {
    // Setup any test initialization if needed
}

/**
 * @brief Main test function for content negotiation algorithm
 */
int run_test_content_negotiation_algorithm(void) {
    UnitySetTestFile(__FILE__);
    test_content_negotiation_algorithm_init();

    // RFC 9110 Specificity Tests
    RUN_TEST(test_rfc9110_exact_match_has_highest_priority);
    RUN_TEST(test_rfc9110_specificity_precedence_type_wildcard);
    RUN_TEST(test_rfc9110_quality_precedence_within_same_specificity);
    RUN_TEST(test_rfc9110_universal_wildcard_matches_everything);
    RUN_TEST(test_rfc9110_type_wildcard_matching);

    // RFC 9110 Edge Cases and Error Handling
    RUN_TEST(test_rfc9110_no_accept_header_fallback);
    RUN_TEST(test_rfc9110_no_matching_server_capabilities_fallback);
    RUN_TEST(test_rfc9110_unacceptable_quality_values);
    RUN_TEST(test_rfc9110_edge_case_empty_server_capabilities);
    RUN_TEST(test_rfc9110_complex_quality_precedence);

    return 0;
}
