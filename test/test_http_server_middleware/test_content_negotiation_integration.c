/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <log.h>

#include "middleware_content_negotiation.h"

/*
 * Content Negotiation Integration Tests
 *
 * Note: These tests use the real httpd_req_t structure directly
 * (not a mock/cast) to avoid structure alignment and casting issues
 * that occurred when using custom mock structures.
 */

/**
 * @brief Test Content Negotiation Middleware Integration
 *
 * Tests the middleware working with mocked HTTP requests and responses.
 */

// Mock functions for testing
static int add_vary_header_call_count = 0;
static char last_vary_header[256] = {0};
static int set_content_type_call_count = 0;

static esp_err_t mock_add_vary_header(httpd_req_t *req, const char *vary_value) {
    (void)req; // Suppress unused parameter warning
    add_vary_header_call_count++;
    strncpy(last_vary_header, vary_value, sizeof(last_vary_header) - 1);
    return ESP_OK;
}

static esp_err_t mock_set_content_type(httpd_req_t *req, const char *content_type) {
    (void)req; (void)content_type; // Suppress unused parameter warnings
    set_content_type_call_count++;
    return ESP_OK;
}

// Test headers and their expected values
static char test_accept_header[1024] = "";
static esp_err_t mock_get_hdr_value_str(httpd_req_t *req, const char *field,
                                      char *val, size_t val_size) {
    (void)req; // Suppress unused parameter warning
    if (strcmp(field, "Accept") == 0) {
        if (test_accept_header[0] != '\0') {
            size_t len = strlen(test_accept_header);
            if (len >= val_size) len = val_size - 1;
            memcpy(val, test_accept_header, len);
            val[len] = '\0';
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void test_middleware_integration_json_preferred(void) {
    // Setup test data
    char *server_types[] = {"application/json", "text/html", "text/plain"};
    httpd_content_negotiation_config_t config = {0};  // Initialize to zero first
    config.capabilities.media_types = server_types;
    config.capabilities.media_type_count = 3;
    config.add_vary_header = mock_add_vary_header;
    config.set_content_type = mock_set_content_type;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request with JSON preference
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strncpy(test_accept_header, "application/json;q=0.9, text/html;q=0.8",
            sizeof(test_accept_header) - 1);

    // Reset counters
    add_vary_header_call_count = 0;
    set_content_type_call_count = 0;
    memset(last_vary_header, 0, sizeof(last_vary_header));

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify negotiation results
    LOGD("test", "req.user_ctx after middleware = %p", req.user_ctx);
    TEST_ASSERT_NOT_NULL(req.user_ctx);
    httpd_content_negotiation_result_t *result = (httpd_content_negotiation_result_t*)req.user_ctx;

    TEST_ASSERT_EQUAL_STRING("application/json", result->selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, result->media_type_score);
    TEST_ASSERT_TRUE(result->vary_header_needed);
    TEST_ASSERT_EQUAL_STRING("Accept", result->vary_header_value);

    // Verify middleware set Vary header
    TEST_ASSERT_EQUAL(1, add_vary_header_call_count);
    TEST_ASSERT_EQUAL_STRING("Accept", last_vary_header);

    // Cleanup
    httpd_free_negotiation_result(result);
    free(result);
}

void test_middleware_integration_application_via_type_wildcard(void) {
    // Setup test data
    char *server_types[] = {"text/html", "application/json", "text/plain"};
    httpd_content_negotiation_config_t config = {0};
    config.capabilities.media_types = server_types;
    config.capabilities.media_type_count = 3;
    config.add_vary_header = mock_add_vary_header;
    config.set_content_type = mock_set_content_type;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request preferring any text type or application type
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strncpy(test_accept_header, "text/*;q=0.8, application/*;q=0.9",
            sizeof(test_accept_header) - 1);

    // Reset counters
    add_vary_header_call_count = 0;
    set_content_type_call_count = 0;

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify negotiation results
    TEST_ASSERT_NOT_NULL(req.user_ctx);
    httpd_content_negotiation_result_t *result = (httpd_content_negotiation_result_t*)req.user_ctx;

    // Should select application/json (highest scoring match per RFC 9110)
    // text/* matches text/html, score = 100 * 0.8 = 80
    // application/* matches application/json, score = 100 * 0.9 = 90 (winner)
    TEST_ASSERT_EQUAL_STRING("application/json", result->selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, result->media_type_score);

    // Cleanup
    httpd_free_negotiation_result(result);
    free(result);
}

void test_middleware_integration_no_accept_header_fallback(void) {
    // Setup test data
    char *server_types[] = {"text/html", "application/json"};
    httpd_content_negotiation_config_t config = {0};
    config.capabilities.media_types = server_types;
    config.capabilities.media_type_count = 2;
    config.add_vary_header = mock_add_vary_header;
    config.set_content_type = mock_set_content_type;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request with no Accept header
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    test_accept_header[0] = '\0';  // Empty Accept header

    // Reset counters
    add_vary_header_call_count = 0;

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify fallback behavior
    TEST_ASSERT_NOT_NULL(req.user_ctx);
    httpd_content_negotiation_result_t *result = (httpd_content_negotiation_result_t*)req.user_ctx;

    TEST_ASSERT_EQUAL_STRING("text/html", result->selected_media_type);  // First server capability
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result->media_type_score);  // Fallback indicator
    TEST_ASSERT_FALSE(result->vary_header_needed);  // No Vary header needed
    TEST_ASSERT_EQUAL(0, add_vary_header_call_count);  // Vary header not set

    // Cleanup
    httpd_free_negotiation_result(result);
    free(result);
}

void test_middleware_integration_no_server_capabilities_skip(void) {
    // Setup config with no capabilities (should skip negotiation)
    httpd_content_negotiation_config_t config = {0};
    config.capabilities.media_types = NULL;
    config.capabilities.media_type_count = 0;
    config.add_vary_header = mock_add_vary_header;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strncpy(test_accept_header, "application/json", sizeof(test_accept_header) - 1);

    // Reset counters
    add_vary_header_call_count = 0;

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify no negotiation occurred
    TEST_ASSERT_NULL(req.user_ctx);  // No result stored
    TEST_ASSERT_EQUAL(0, add_vary_header_call_count);  // No headers set
}

void test_middleware_integration_vary_header_cache_correctness(void) {
    // Setup test data
    char *server_types[] = {"application/json", "text/html"};
    httpd_content_negotiation_config_t config = {0};
    config.capabilities.media_types = server_types;
    config.capabilities.media_type_count = 2;
    config.add_vary_header = mock_add_vary_header;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request that triggers negotiation
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strncpy(test_accept_header, "application/json;q=0.5, text/html;q=0.9",
            sizeof(test_accept_header) - 1);

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify Vary header set for cache correctness
    TEST_ASSERT_EQUAL(1, add_vary_header_call_count);
    TEST_ASSERT_EQUAL_STRING("Accept", last_vary_header);

    // Verify result indicates negotiation occurred
    httpd_content_negotiation_result_t *result = (httpd_content_negotiation_result_t*)req.user_ctx;
    TEST_ASSERT_TRUE(result->vary_header_needed);
    TEST_ASSERT_EQUAL_STRING("Accept", result->vary_header_value);

    // Cleanup
    httpd_free_negotiation_result(result);
    free(result);
}

void test_handler_api_access_negotiated_content(void) {
    // Setup test data
    char *server_types[] = {"application/json", "text/html"};
    httpd_content_negotiation_config_t config = {0};
    config.capabilities.media_types = server_types;
    config.capabilities.media_type_count = 2;
    config.add_vary_header = mock_add_vary_header;
    config.req_get_hdr_value_str = mock_get_hdr_value_str;

    // Mock request with negotiation
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strncpy(test_accept_header, "application/json", sizeof(test_accept_header) - 1);

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Test handler API functions
    httpd_req_t *req_ptr = &req;

    TEST_ASSERT_TRUE(middleware_content_was_negotiated(req_ptr));
    TEST_ASSERT_EQUAL_STRING("application/json",
                           middleware_get_negotiated_content_type(req_ptr));
    TEST_ASSERT_NULL(middleware_get_negotiated_encoding(req_ptr));  // Future feature
    TEST_ASSERT_NULL(middleware_get_negotiated_language(req_ptr));  // Future feature
    TEST_ASSERT_NULL(middleware_get_negotiated_charset(req_ptr));   // Future feature

    // Cleanup
    httpd_content_negotiation_result_t *result = (httpd_content_negotiation_result_t*)req.user_ctx;
    httpd_free_negotiation_result(result);
    free(result);
}

void test_handler_api_no_negotiation_context(void) {
    // Mock request without negotiation context
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    httpd_req_t *req_ptr = &req;

    // Test handler API functions with no context
    TEST_ASSERT_FALSE(middleware_content_was_negotiated(req_ptr));
    TEST_ASSERT_NULL(middleware_get_negotiated_content_type(req_ptr));
    TEST_ASSERT_NULL(middleware_get_negotiated_encoding(req_ptr));
    TEST_ASSERT_NULL(middleware_get_negotiated_language(req_ptr));
    TEST_ASSERT_NULL(middleware_get_negotiated_charset(req_ptr));
}

void test_middleware_configuration_error_handling(void) {
    // Test middleware with NULL config
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    esp_err_t err = middleware_content_negotiation(&req, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    // Test with NULL request
    httpd_content_negotiation_config_t config = {0};
    err = middleware_content_negotiation(NULL, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

/**
 * @brief Initialize content negotiation integration tests
 */
void test_content_negotiation_integration_init(void) {
    // Reset test state
    add_vary_header_call_count = 0;
    set_content_type_call_count = 0;
    memset(last_vary_header, 0, sizeof(last_vary_header));
    memset(test_accept_header, 0, sizeof(test_accept_header));
}

/**
 * @brief Main test function for content negotiation integration
 */
int run_test_content_negotiation_integration(void) {
    UnitySetTestFile(__FILE__);
    test_content_negotiation_integration_init();

    // Middleware Integration Tests
    RUN_TEST(test_middleware_integration_json_preferred);
    RUN_TEST(test_middleware_integration_application_via_type_wildcard);
    RUN_TEST(test_middleware_integration_no_accept_header_fallback);
    RUN_TEST(test_middleware_integration_no_server_capabilities_skip);
    RUN_TEST(test_middleware_integration_vary_header_cache_correctness);

    // Handler API Tests
    RUN_TEST(test_handler_api_access_negotiated_content);
    RUN_TEST(test_handler_api_no_negotiation_context);

    // Error Handling Tests
    RUN_TEST(test_middleware_configuration_error_handling);

    return 0;
}
