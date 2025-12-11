#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/http-server-middleware/include/middleware_auth.h"

// Mocking support for Auth testing
static char mock_auth_header[128] = "";
static esp_err_t mock_get_hdr_result = ESP_OK;

static char mock_response_status_str[50] = "";
static char mock_response_headers[32][128]; // Store up to 32 headers
static char mock_response_values[32][128];
static int mock_response_hdr_count = 0;
static char mock_response_body[1024] = "";
static httpd_err_code_t mock_response_err_code = 0;


// Mock implementations for Auth testing
static esp_err_t mock_httpd_req_get_hdr_value_str(httpd_req_t *req, const char *hdr_name, char *val, size_t val_size) {
    if (strcmp(hdr_name, "Authorization") == 0) {
        if (mock_get_hdr_result == ESP_OK && strlen(mock_auth_header) > 0) {
            strncpy(val, mock_auth_header, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
        return ESP_FAIL;
    }
    return ESP_FAIL;
}

static esp_err_t mock_httpd_resp_set_status(httpd_req_t *req, const char *status) {
    strncpy(mock_response_status_str, status, sizeof(mock_response_status_str) - 1);
    mock_response_status_str[sizeof(mock_response_status_str) - 1] = '\0';
    return ESP_OK;
}

static esp_err_t mock_httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value) {
    if (mock_response_hdr_count < 32) {
        strncpy(mock_response_headers[mock_response_hdr_count], field, sizeof(mock_response_headers[0]) - 1);
        mock_response_headers[mock_response_hdr_count][sizeof(mock_response_headers[0]) - 1] = '\0';
        strncpy(mock_response_values[mock_response_hdr_count], value, sizeof(mock_response_values[0]) - 1);
        mock_response_values[mock_response_hdr_count][sizeof(mock_response_values[0]) - 1] = '\0';
        mock_response_hdr_count++;
    }
    return ESP_OK;
}

static esp_err_t mock_httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message) {
    mock_response_err_code = error;
    strncpy(mock_response_body, message, sizeof(mock_response_body) - 1);
    mock_response_body[sizeof(mock_response_body) - 1] = '\0';
    return ESP_FAIL; // Indicate error occurred
}

void reset_auth_mocks() {
    memset(mock_auth_header, 0, sizeof(mock_auth_header));
    mock_get_hdr_result = ESP_OK;
    memset(mock_response_status_str, 0, sizeof(mock_response_status_str));
    memset(mock_response_headers, 0, sizeof(mock_response_headers));
    memset(mock_response_values, 0, sizeof(mock_response_values));
    mock_response_hdr_count = 0;
    memset(mock_response_body, 0, sizeof(mock_response_body));
    mock_response_err_code = 0;
}

// Helper to find header in response
static const char* get_auth_response_header(const char *hdr_name) {
    for (int i = 0; i < mock_response_hdr_count; i++) {
        if (strcmp(mock_response_headers[i], hdr_name) == 0) {
            return mock_response_values[i];
        }
    }
    return NULL;
}

// Helper to create test config with mock callbacks
static auth_config_t create_test_config(const char *username, const char *password, bool allow_public) {
    auth_config_t config = {
        .username = username,
        .password = password,
        .allow_public = allow_public,
        .req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str,
        .resp_set_status = mock_httpd_resp_set_status,
        .resp_set_hdr = mock_httpd_resp_set_hdr,
        .resp_send_err = mock_httpd_resp_send_err
    };
    return config;
}

/* Test auth middleware with valid authorization header (but not basic) */
void test_auth_with_non_basic_authorization_header(void) {
    // Arrange
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("admin", "password", false);
    strcpy(mock_auth_header, "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."); // Non-Basic auth

    // Act
    esp_err_t result = middleware_auth(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Basic authentication required", mock_response_body);
}

/* Test auth middleware with valid basic authentication */
void test_auth_with_valid_basic_authorization_header(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("user", "pass", false);
    // "user:pass" base64 encoded
    strcpy(mock_auth_header, "Basic dXNlcjpwYXNz");

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_OK, result);
    // No error indicators should be set
    TEST_ASSERT_EQUAL(0, strlen(mock_response_status_str));
    TEST_ASSERT_NULL(get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL(0, strlen(mock_response_body));
}

/* Test auth middleware with invalid basic authentication */
void test_auth_with_invalid_basic_authorization_header(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("user", "pass", false);
    // "wrong:creds" base64 encoded
    strcpy(mock_auth_header, "Basic d2dhd2U6dHlwY2hhdA==");

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", mock_response_body);
}

/* Test auth middleware with missing authorization header */
void test_auth_missing_authorization_header(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("user", "pass", false);
    mock_get_hdr_result = ESP_FAIL; // Simulate missing header

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Authentication required", mock_response_body);
}

/* Test auth middleware skips public endpoints */
void test_auth_skips_public_endpoints(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/public/data");

    auth_config_t config = create_test_config("user", "pass", true); // allow_public = true
    mock_get_hdr_result = ESP_FAIL; // Simulate missing header, should be skipped

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_OK, result);
    // No error indicators should be set as public endpoint skipped auth
    TEST_ASSERT_EQUAL(0, strlen(mock_response_status_str));
    TEST_ASSERT_NULL(get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL(0, strlen(mock_response_body));
}

/* Test auth middleware with incorrect base64 format */
void test_auth_incorrect_base64_format(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("user", "pass", false);
    strcpy(mock_auth_header, "Basic NotValidBase64="); // Invalid Base64

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials format", mock_response_body);
}

/* Test auth middleware with no colon in credentials */
void test_auth_no_colon_in_credentials(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config("user", "pass", false);
    // "userpass" base64 encoded (no colon)
    strcpy(mock_auth_header, "Basic dXNlcnBhc3M=");

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials format", mock_response_body);
}

int test_auth() {
    UNITY_BEGIN();

    RUN_TEST(test_auth_with_non_basic_authorization_header);
    RUN_TEST(test_auth_with_valid_basic_authorization_header);
    RUN_TEST(test_auth_with_invalid_basic_authorization_header);
    RUN_TEST(test_auth_missing_authorization_header);
    RUN_TEST(test_auth_skips_public_endpoints);
    RUN_TEST(test_auth_incorrect_base64_format);
    RUN_TEST(test_auth_no_colon_in_credentials);

    return UNITY_END();
}
