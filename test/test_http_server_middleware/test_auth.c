#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/http-server-middleware/include/middleware_auth.h"
#include "../../lib/base64/base64_codec.h"

// Mocking support for Auth testing
static char mock_auth_header[512] = "";
static esp_err_t mock_get_hdr_result = ESP_OK;

static char mock_response_status_str[50] = "";
static char mock_response_headers[32][128]; // Store up to 32 headers
static char mock_response_values[32][128];
static int mock_response_hdr_count = 0;
static char mock_response_body[1024] = "";
static httpd_err_code_t mock_response_err_code = 0;

// New mocks for callback-based auth
static bool mock_requires_auth_result = true;
static const char *mock_requires_auth_uri = NULL;
static esp_err_t mock_check_credentials_result = ESP_OK;
static char mock_check_username_buf[64] = "";
static char mock_check_password_buf[64] = "";
static const char *mock_check_username = NULL;
static const char *mock_check_password = NULL;


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

// Existing mocks...

static esp_err_t mock_httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message) {
    mock_response_err_code = error;
    strncpy(mock_response_body, message, sizeof(mock_response_body) - 1);
    mock_response_body[sizeof(mock_response_body) - 1] = '\0';
    return ESP_FAIL; // Indicate error occurred
}

// New mock callbacks
static bool mock_requires_auth(const char *uri, void *ctx) {
    mock_requires_auth_uri = uri;
    return mock_requires_auth_result;
}

static esp_err_t mock_check_credentials(const char *username, const char *password, void *ctx) {
    strncpy(mock_check_username_buf, username ? username : "", sizeof(mock_check_username_buf) - 1);
    mock_check_username_buf[sizeof(mock_check_username_buf) - 1] = '\0';
    mock_check_username = mock_check_username_buf;

    strncpy(mock_check_password_buf, password ? password : "", sizeof(mock_check_password_buf) - 1);
    mock_check_password_buf[sizeof(mock_check_password_buf) - 1] = '\0';
    mock_check_password = mock_check_password_buf;

    return mock_check_credentials_result;
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

    // Reset new callback mocks
    mock_requires_auth_result = true;
    mock_requires_auth_uri = NULL;
    mock_check_credentials_result = ESP_OK;
    memset(mock_check_username_buf, 0, sizeof(mock_check_username_buf));
    memset(mock_check_password_buf, 0, sizeof(mock_check_password_buf));
    mock_check_username = NULL;
    mock_check_password = NULL;
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
static auth_config_t create_test_config(void) {
    auth_config_t config = {
        .requires_auth = mock_requires_auth,
        .bypass_ctx = NULL,
        .check_credentials = mock_check_credentials,
        .check_ctx = NULL,
        .req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str,
        .resp_set_status = mock_httpd_resp_set_status,
        .resp_set_hdr = mock_httpd_resp_set_hdr,
        .resp_send_err = mock_httpd_resp_send_err
    };
    return config;
}

// Helper to set Basic Auth header from username:password
static void set_basic_auth_header(const char *username, const char *password) {
    char creds[128];
    snprintf(creds, sizeof(creds), "%s:%s", username, password);
    char encoded[256];
    size_t encoded_len = base64_encode((const unsigned char *)creds, strlen(creds), (char *)encoded, sizeof(encoded));
    snprintf(mock_auth_header, sizeof(mock_auth_header), "Basic %s", encoded);
}

/* Test auth middleware with non-basic authorization header (requires auth path) */
void test_auth_with_non_basic_authorization_header(void) {
    // Arrange
    reset_auth_mocks();
    mock_requires_auth_result = true;  // Require auth
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    strcpy(mock_auth_header, "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."); // Non-Basic auth

    // Act
    esp_err_t result = middleware_auth(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Basic authentication required", mock_response_body);
}

/* Test auth middleware with valid basic authentication (requires auth path) */
void test_auth_with_valid_basic_authorization_header(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;  // Require auth
    mock_check_credentials_result = ESP_OK;
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    set_basic_auth_header("user", "pass");

    esp_err_t result = middleware_auth(&req, NULL, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("user", mock_check_username);
    TEST_ASSERT_EQUAL_STRING("pass", mock_check_password);
    // No error indicators should be set
    TEST_ASSERT_EQUAL(0, strlen(mock_response_status_str));
    TEST_ASSERT_NULL(get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL(0, strlen(mock_response_body));
}

/* Test auth middleware with invalid basic authentication (callback denies) */
void test_auth_with_invalid_basic_authorization_header(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;  // Require auth
    mock_check_credentials_result = ESP_FAIL;
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    set_basic_auth_header("bad", "bad");

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("bad", mock_check_username);
    TEST_ASSERT_EQUAL_STRING("bad", mock_check_password);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", mock_response_body);
}

/* Test auth middleware with missing authorization header (requires auth path) */
void test_auth_missing_authorization_header(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;  // Require auth
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    mock_get_hdr_result = ESP_FAIL; // Simulate missing header

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Authentication required", mock_response_body);
}

/* Test auth middleware bypasses via requires_auth callback */
void test_auth_bypass_via_requires_auth_callback(void) {
    reset_auth_mocks();
    mock_requires_auth_result = false;  // Bypass auth
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/public/data");

    auth_config_t config = create_test_config();
    mock_get_hdr_result = ESP_FAIL; // No header, but should bypass anyway

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("/public/data", mock_requires_auth_uri);
    // No error indicators, no creds check
    TEST_ASSERT_NULL(mock_check_username);
    TEST_ASSERT_EQUAL(0, strlen(mock_response_status_str));
    TEST_ASSERT_NULL(get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL(0, strlen(mock_response_body));
}

/* Test auth middleware with incorrect base64 format (requires auth) */
void test_auth_incorrect_base64_format(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    strcpy(mock_auth_header, "Basic NotValidBase64="); // Invalid Base64

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials format", mock_response_body);
    TEST_ASSERT_NULL(mock_check_username);  // No callback call
}

/* Test auth middleware with no colon in credentials (requires auth) */
void test_auth_no_colon_in_credentials(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    // "userpass" (no colon) base64 encoded
    set_basic_auth_header("userpass", "");  // "" password creates "userpass:" but wait, no - to test no colon, need special
    // Manual for no colon
    char creds_no_colon[] = "userpass";
    char encoded_no_colon[256];
    base64_encode((const unsigned char *)creds_no_colon, strlen(creds_no_colon), (char *)encoded_no_colon, sizeof(encoded_no_colon));
    snprintf(mock_auth_header, sizeof(mock_auth_header), "Basic %s", encoded_no_colon);

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", get_auth_response_header("WWW-Authenticate"));
    TEST_ASSERT_EQUAL_STRING("Invalid credentials format", mock_response_body);
    TEST_ASSERT_NULL(mock_check_username);  // No callback call
}

/* Test null requires_auth callback requires auth for all paths */
void test_auth_null_requires_auth_requires_all(void) {
    reset_auth_mocks();
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/public/data");

    auth_config_t config = {
        .requires_auth = NULL,  // NULL = require for all
        .bypass_ctx = NULL,
        .check_credentials = mock_check_credentials,
        .check_ctx = NULL,
        .req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str,
        .resp_set_status = mock_httpd_resp_set_status,
        .resp_set_hdr = mock_httpd_resp_set_hdr,
        .resp_send_err = mock_httpd_resp_send_err
    };
    mock_get_hdr_result = ESP_FAIL;  // Missing header -> should 401 since requires auth

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
}

/* Test null check_credentials denies access */
void test_auth_null_check_credentials_denies(void) {
    reset_auth_mocks();
    mock_requires_auth_result = true;
    struct httpd_req req;
    memset(&req, 0, sizeof(req));
    strcpy((char *)req.uri, "/api/protected");

    auth_config_t config = create_test_config();
    config.check_credentials = NULL;  // NULL callback denies
    set_basic_auth_header("user", "pass");  // Valid header

    esp_err_t result = middleware_auth(&req, NULL, &config);

    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("401 Unauthorized", mock_response_status_str);
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", mock_response_body);
}

int test_auth() {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_auth_with_non_basic_authorization_header);
    RUN_TEST(test_auth_with_valid_basic_authorization_header);
    RUN_TEST(test_auth_with_invalid_basic_authorization_header);
    RUN_TEST(test_auth_missing_authorization_header);
    RUN_TEST(test_auth_bypass_via_requires_auth_callback);
    RUN_TEST(test_auth_incorrect_base64_format);
    RUN_TEST(test_auth_no_colon_in_credentials);
    RUN_TEST(test_auth_null_requires_auth_requires_all);
    RUN_TEST(test_auth_null_check_credentials_denies);

    // return UNITY_END();
    return 0;
}
