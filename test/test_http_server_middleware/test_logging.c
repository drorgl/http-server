#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <middleware_logging.h>

// Mock logging output
static char mock_log_buffer[1024] = {0};
static int mock_log_count = 0;

// Mock std::printf implementation
static int mock_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    int result = vsprintf(mock_log_buffer + strlen(mock_log_buffer), format, args);
    va_end(args);

    mock_log_count++;
    return result;
}

// Mock req_get_hdr_value_str that simulates finding a User-Agent header
static esp_err_t mock_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size) {
    if (strcmp(field, "User-Agent") == 0) {
        strncpy(val, "TestAgent/1.0", val_size - 1);
        val[val_size - 1] = '\0';
        return ESP_OK;
    }
    return ESP_FAIL; // Other headers not found
}

const char* mock_http_method_str(httpd_method_t method) {
    // Default mock implementation
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD: return "HEAD";
        case HTTP_OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

// Helper function to create a mock logging config
static logging_config_t create_mock_logging_config(int log_level) {
    logging_config_t config = {
        .log_level = log_level,
        .req_get_hdr_value_str = mock_req_get_hdr_value_str,
        .method_str = mock_http_method_str,  // Not needed for basic tests
        .printf = mock_printf
    };
    return config;
}

// Helper to reset mock state
static void reset_mock_logging_state(void) {
    memset(mock_log_buffer, 0, sizeof(mock_log_buffer));
    mock_log_count = 0;
}

/* Test logging middleware with GET request */
void test_logging_get_request(void) {
    // Arrange
    reset_mock_logging_state();

    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/users");
    req.method = HTTP_GET;

    logging_config_t config = create_mock_logging_config(1);  // Level 1 logging

    // Act - Call logging middleware with proper config
    esp_err_t result = middleware_logging(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_EQUAL(0, mock_log_count);  // Should have logged something
}

/* Additional test with different log levels */
void test_logging_different_levels(void) {
    // Test level 2 logging (includes headers)
    reset_mock_logging_state();

    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;
    req.content_len = 1024;

    logging_config_t config = create_mock_logging_config(2);  // Level 2 logging

    esp_err_t result = middleware_logging(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_EQUAL(0, mock_log_count);
}

/* Test logging with level 3 (maximum verbosity) */
void test_logging_max_level(void) {
    // Test level 3 logging (includes content length)
    reset_mock_logging_state();

    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;
    req.content_len = 2048;

    logging_config_t config = create_mock_logging_config(3);  // Level 3 logging

    esp_err_t result = middleware_logging(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_EQUAL(0, mock_log_count);
}

int test_logging() {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_logging_get_request);
    RUN_TEST(test_logging_different_levels);
    RUN_TEST(test_logging_max_level);

    // return UNITY_END();
    return 0;
}
