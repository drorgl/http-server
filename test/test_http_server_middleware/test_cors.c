#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <middleware_cors.h>

// Mocking support for CORS testing
static int mock_get_hdr_call_count = 0;
static char mock_origin_header[128] = "";
static char mock_request_headers[32][128]; // Store up to 32 headers
static int mock_header_count = 0;
static char mock_response_headers[32][128];
static char mock_response_values[32][128];
static char mock_response_body[1024] = "";
static int mock_response_status_int = 0;
static char mock_response_status_str[50] = "";

// Mock implementations for CORS testing
static esp_err_t mock_httpd_req_get_hdr_value_str(httpd_req_t *req, const char *hdr_name, char *val, size_t val_size) {
    mock_get_hdr_call_count++;
    if (strcmp(hdr_name, "Origin") == 0) {
        if (strlen(mock_origin_header) > 0) {
            strncpy(val, mock_origin_header, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    }
    return ESP_FAIL; // Header not found
}

static esp_err_t mock_httpd_resp_set_hdr(httpd_req_t *req, const char *hdr_name, const char *hdr_value) {
    if (mock_header_count < 32) {
        strcpy(mock_response_headers[mock_header_count], hdr_name);
        strcpy(mock_response_values[mock_header_count], hdr_value);
        mock_header_count++;
    }
    return ESP_OK;
}

static esp_err_t mock_httpd_resp_set_status(httpd_req_t *req, const char *status) {
    strcpy(mock_response_status_str, status);
    return ESP_OK;
}

static esp_err_t mock_httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len) {
    if (buf) {
        strncpy(mock_response_body, buf, sizeof(mock_response_body) - 1);
    }
    return ESP_OK;
}

void reset_cors_mocks() {
    mock_get_hdr_call_count = 0;
    memset(mock_origin_header, 0, sizeof(mock_origin_header));
    mock_header_count = 0;
    memset(mock_response_headers, 0, sizeof(mock_response_headers));
    memset(mock_response_values, 0, sizeof(mock_response_values));
    memset(mock_response_body, 0, sizeof(mock_response_body));
    mock_response_status_int = 0;
    strcpy(mock_response_status_str, "");
}

// Helper to find header in response
static const char* get_response_header(const char *hdr_name) {
    for (int i = 0; i < mock_header_count; i++) {
        if (strcmp(mock_response_headers[i], hdr_name) == 0) {
            return mock_response_values[i];
        }
    }
    return NULL;
}

// Mock HTTP request/response functions for testing

static esp_err_t mock_httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message) {
    mock_response_status_int = error;
    strcpy(mock_response_body, message);
    return ESP_OK;
}

/**
 * @brief Helper function to check if origin is in allowed list
 */
static bool cors_origin_allowed(const char *origin, const char *allowed_origins)
{
    if (strcmp(allowed_origins, "*") == 0) {
        return true;
    }

    // Simple comma-separated list check (could be optimized)
    const char *ptr = allowed_origins;
    while (*ptr) {
        const char *start = ptr;
        while (*ptr && *ptr != ',') {
            ptr++;
        }

        size_t len = ptr - start;
        if (strncmp(origin, start, len) == 0 && strlen(origin) == len) {
            return true;
        }

        if (*ptr == ',') ptr++; // Skip comma and space
        while (*ptr == ' ') ptr++; // Skip spaces after comma
    }

    return false;
}

/**
 * @brief CORS middleware implementation
 *
 * Handles preflight OPTIONS requests and adds CORS headers to responses
 */
// We can now test the REAL middleware_cors() function with mock operations!
// No more internal test functions needed.

// Helper to create test config with mock callbacks
static cors_config_t create_test_config(const char *allowed_origins, const char *allowed_methods, const char *allowed_headers, bool allow_credentials, int max_age) {
    cors_config_t config = {
        .allowed_origins = allowed_origins,
        .allowed_methods = allowed_methods,
        .allowed_headers = allowed_headers,
        .allow_credentials = allow_credentials,
        .max_age = max_age,
        .req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str,
        .resp_send_err = mock_httpd_resp_send_err,
        .resp_set_status = mock_httpd_resp_set_status,
        .resp_set_hdr = mock_httpd_resp_set_hdr,
        .resp_send = mock_httpd_resp_send
    };
    return config;
}

// Test CORS middleware functions directly
/* Test CORS basic functionality */
void test_cors_allowed_origin(void) {
    // Arrange
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;

    cors_config_t config = create_test_config("https://example.com,https://test.com", "GET,POST,OPTIONS", "Content-Type,Authorization", true, 3600);

    // Set mock Origin header
    strcpy(mock_origin_header, "https://example.com");

    // Act - Calling the REAL middleware_cors() function!
    esp_err_t result = middleware_cors(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("https://example.com", get_response_header("Access-Control-Allow-Origin"));
    TEST_ASSERT_EQUAL_STRING("true", get_response_header("Access-Control-Allow-Credentials"));
}

/* Test CORS wildcard origin */
void test_cors_wildcard_origin(void) {
    // Arrange
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;

    reset_cors_mocks();

    cors_config_t config = create_test_config("*", "GET,POST,OPTIONS", "Content-Type", false, 0);

    strcpy(mock_origin_header, "https://any-origin.com");

    // Act - Using REAL middleware_cors()!
    esp_err_t result = middleware_cors(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("https://any-origin.com", get_response_header("Access-Control-Allow-Origin"));
}

/* Test CORS rejected origin */
void test_cors_rejected_origin(void) {
    // Arrange
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;

    reset_cors_mocks();

    cors_config_t config = create_test_config("https://allowed.com", "GET,POST,OPTIONS", "Content-Type", false, 0);

    strcpy(mock_origin_header, "https://malicious.com");

    // Act - Using REAL middleware_cors()!
    esp_err_t result = middleware_cors(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(HTTPD_403_FORBIDDEN, mock_response_status_int);
    TEST_ASSERT_EQUAL_STRING("Origin not allowed", mock_response_body);
}

/* Test CORS preflight OPTIONS request */
void test_cors_preflight_options(void) {
    // Arrange
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_OPTIONS;

    cors_config_t config = create_test_config("https://example.com", "GET,POST,PUT,DELETE", "Content-Type,Authorization,X-Custom", true, 86400);

    reset_cors_mocks();

    strcpy(mock_origin_header, "https://example.com");

    // Act - Using REAL middleware_cors()!
    esp_err_t result = middleware_cors(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("200 OK", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("https://example.com", get_response_header("Access-Control-Allow-Origin"));
    TEST_ASSERT_EQUAL_STRING("GET,POST,PUT,DELETE", get_response_header("Access-Control-Allow-Methods"));
    TEST_ASSERT_EQUAL_STRING("Content-Type,Authorization,X-Custom", get_response_header("Access-Control-Allow-Headers"));
    TEST_ASSERT_EQUAL_STRING("true", get_response_header("Access-Control-Allow-Credentials"));
    TEST_ASSERT_EQUAL_STRING("86400", get_response_header("Access-Control-Max-Age"));
}

/* Test CORS without Origin header */
void test_cors_no_origin_header(void) {
    // Arrange
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/test");
    req.method = HTTP_GET;

    cors_config_t config = create_test_config("https://example.com", "GET,POST", "Content-Type", false, 0);

    reset_cors_mocks();

    // No origin header set (empty string)

    // Act - Using REAL middleware_cors()!
    esp_err_t result = middleware_cors(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    // No CORS headers should be set
    TEST_ASSERT_NULL(get_response_header("Access-Control-Allow-Origin"));
}

int test_cors() {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_cors_allowed_origin);
    RUN_TEST(test_cors_wildcard_origin);
    RUN_TEST(test_cors_rejected_origin);
    RUN_TEST(test_cors_preflight_options);
    RUN_TEST(test_cors_no_origin_header);

    // return UNITY_END();
    return 0;
}
