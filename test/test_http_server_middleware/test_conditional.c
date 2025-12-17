/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <middleware_conditional.h>

// Test header values for mocking
static char test_if_match_value[256] = "";
static char test_if_none_match_value[256] = "";
static char test_if_modified_since_value[256] = "";
static char test_if_unmodified_since_value[256] = "";

// Helper function to set test conditional headers
void set_test_conditional_header(const char *header_name, const char *header_value) {
    if (!header_name) return;

    if (strcmp(header_name, "If-Match") == 0) {
        if (header_value) {
            strncpy(test_if_match_value, header_value, sizeof(test_if_match_value) - 1);
            test_if_match_value[sizeof(test_if_match_value) - 1] = '\0';
        } else {
            test_if_match_value[0] = '\0';
        }
        httpd_set_test_conditional_header(header_name, header_value);
    } else if (strcmp(header_name, "If-None-Match") == 0) {
        if (header_value) {
            strncpy(test_if_none_match_value, header_value, sizeof(test_if_none_match_value) - 1);
            test_if_none_match_value[sizeof(test_if_none_match_value) - 1] = '\0';
        } else {
            test_if_none_match_value[0] = '\0';
        }
        httpd_set_test_conditional_header(header_name, header_value);
    } else if (strcmp(header_name, "If-Modified-Since") == 0) {
        if (header_value) {
            strncpy(test_if_modified_since_value, header_value, sizeof(test_if_modified_since_value) - 1);
            test_if_modified_since_value[sizeof(test_if_modified_since_value) - 1] = '\0';
        } else {
            test_if_modified_since_value[0] = '\0';
        }
        httpd_set_test_conditional_header(header_name, header_value);
    } else if (strcmp(header_name, "If-Unmodified-Since") == 0) {
        if (header_value) {
            strncpy(test_if_unmodified_since_value, header_value, sizeof(test_if_unmodified_since_value) - 1);
            test_if_unmodified_since_value[sizeof(test_if_unmodified_since_value) - 1] = '\0';
        } else {
            test_if_unmodified_since_value[0] = '\0';
        }
        httpd_set_test_conditional_header(header_name, header_value);
    }
}

// Mock data for testing
static httpd_req_t dummy_req = {0};
static char mock_response_status_str[50] = "";
static char mock_response_headers[32][128];  // Store up to 32 headers
static char mock_response_values[32][128];
static int mock_response_hdr_count = 0;
static char mock_response_body[2048] = "";
static size_t mock_response_body_len = 0;

// Mock implementations for testing - using local test variables
static esp_err_t mock_httpd_req_get_hdr_value_str(httpd_req_t *req, const char *hdr_name, char *val, size_t val_size) {
    // For testing, we'll check if test values are set
    if (strcmp(hdr_name, "If-Match") == 0) {
        if (strlen(test_if_match_value) > 0) {
            strncpy(val, test_if_match_value, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    } else if (strcmp(hdr_name, "If-None-Match") == 0) {
        if (strlen(test_if_none_match_value) > 0) {
            strncpy(val, test_if_none_match_value, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    } else if (strcmp(hdr_name, "If-Modified-Since") == 0) {
        if (strlen(test_if_modified_since_value) > 0) {
            strncpy(val, test_if_modified_since_value, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    } else if (strcmp(hdr_name, "If-Unmodified-Since") == 0) {
        if (strlen(test_if_unmodified_since_value) > 0) {
            strncpy(val, test_if_unmodified_since_value, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static size_t mock_req_get_hdr_value_len(httpd_req_t *req, const char *field) {
    // For testing, we'll check if test values are set
    if (strcmp(field, "If-Match") == 0) {
        return strlen(test_if_match_value);
    } else if (strcmp(field, "If-None-Match") == 0) {
        return strlen(test_if_none_match_value);
    } else if (strcmp(field, "If-Modified-Since") == 0) {
        return strlen(test_if_modified_since_value);
    } else if (strcmp(field, "If-Unmodified-Since") == 0) {
        return strlen(test_if_unmodified_since_value);
    }
    return 0;
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

static esp_err_t mock_httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len) {
    if (buf) {
        size_t copy_len = buf_len == HTTPD_RESP_USE_STRLEN ? strlen(buf) : (size_t)buf_len;
        if (copy_len > sizeof(mock_response_body) - 1) {
            copy_len = sizeof(mock_response_body) - 1;
        }
        memcpy(mock_response_body, buf, copy_len);
        mock_response_body[copy_len] = '\0';
        mock_response_body_len = copy_len;
    } else {
        mock_response_body[0] = '\0';
        mock_response_body_len = 0;
    }
    return ESP_OK;
}

void reset_conditional_mocks() {
    // Clear test headers (using local mock)
    set_test_conditional_header("If-Match", NULL);
    set_test_conditional_header("If-None-Match", NULL);
    set_test_conditional_header("If-Modified-Since", NULL);
    set_test_conditional_header("If-Unmodified-Since", NULL);

    memset(mock_response_status_str, 0, sizeof(mock_response_status_str));
    memset(mock_response_headers, 0, sizeof(mock_response_headers));
    memset(mock_response_values, 0, sizeof(mock_response_values));
    mock_response_hdr_count = 0;
    memset(mock_response_body, 0, sizeof(mock_response_body));
    mock_response_body_len = 0;
}

// Helper to find header in response
static const char* get_conditional_response_header(const char *hdr_name) {
    for (int i = 0; i < mock_response_hdr_count; i++) {
        if (strcmp(mock_response_headers[i], hdr_name) == 0) {
            return mock_response_values[i];
        }
    }
    return NULL;
}

// Mock ETag generator for testing
static esp_err_t mock_etag_generator(httpd_req_t *req, char *etag, size_t etag_len) {
    if (!etag || etag_len < 10) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(etag, "\"test-etag\"", etag_len - 1);
    etag[etag_len - 1] = '\0';
    return ESP_OK;
}

// Mock Last-Modified function for testing
static esp_err_t mock_last_modified_fn(httpd_req_t *req, long long *last_modified) {
    if (!last_modified) {
        return ESP_ERR_INVALID_ARG;
    }
    *last_modified = 1609459200; // 2021-01-01 00:00:00 UTC
    return ESP_OK;
}

// Test ETag generation functions
void test_generate_strong_etag_success(void) {
    char etag[HTTPD_MAX_ETAG_LEN];
    const char *content = "Hello, World!";
    esp_err_t result = httpd_generate_strong_etag(content, strlen(content), etag, sizeof(etag));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(etag);
    TEST_ASSERT_TRUE(strlen(etag) > 0);
}

void test_generate_strong_etag_invalid_args(void) {
    char etag[HTTPD_MAX_ETAG_LEN];
    const char *content = "Hello, World!";
    
    // Test with NULL content
    esp_err_t result = httpd_generate_strong_etag(NULL, strlen(content), etag, sizeof(etag));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Test with NULL etag
    result = httpd_generate_strong_etag(content, strlen(content), NULL, sizeof(etag));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Test with insufficient buffer size
    result = httpd_generate_strong_etag(content, strlen(content), etag, 5);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
}

void test_generate_weak_etag_success(void) {
    char etag[HTTPD_MAX_ETAG_LEN];
    long long timestamp = 1609459200;
    esp_err_t result = httpd_generate_weak_etag(timestamp, etag, sizeof(etag));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(etag);
    TEST_ASSERT_TRUE(strlen(etag) > 0);
    TEST_ASSERT_TRUE(strstr(etag, "W/") != NULL);
}

void test_generate_weak_etag_invalid_args(void) {
    char etag[HTTPD_MAX_ETAG_LEN];
    long long timestamp = 1609459200;
    
    // Test with NULL etag
    esp_err_t result = httpd_generate_weak_etag(timestamp, NULL, sizeof(etag));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Test with insufficient buffer size
    result = httpd_generate_weak_etag(timestamp, etag, 5);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
}

// Test ETag parsing
// These tests use internal functions that are not exposed in the public API
// They are kept for documentation but commented out to avoid compilation errors
/*
void test_parse_etag_list_single_etag(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 0;
    const char *header_value = "\"test-etag\"";
    
    // This function is internal and not exposed in the public API
    // esp_err_t result = parse_etag_list(header_value, etags, &etag_count, 10);
    // TEST_ASSERT_EQUAL(ESP_OK, result);
    // TEST_ASSERT_EQUAL(1, etag_count);
    // TEST_ASSERT_EQUAL_STRING("\"test-etag\"", etags[0]);
}

void test_parse_etag_list_multiple_etags(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 0;
    const char *header_value = "\"etag1\", \"etag2\", \"etag3\"";
    
    // This function is internal and not exposed in the public API
    // esp_err_t result = parse_etag_list(header_value, etags, &etag_count, 10);
    // TEST_ASSERT_EQUAL(ESP_OK, result);
    // TEST_ASSERT_EQUAL(3, etag_count);
    // TEST_ASSERT_EQUAL_STRING("\"etag1\"", etags[0]);
    // TEST_ASSERT_EQUAL_STRING("\"etag2\"", etags[1]);
    // TEST_ASSERT_EQUAL_STRING("\"etag3\"", etags[2]);
}

void test_parse_etag_list_star(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 0;
    const char *header_value = "*";
    
    // This function is internal and not exposed in the public API
    // esp_err_t result = parse_etag_list(header_value, etags, &etag_count, 10);
    // TEST_ASSERT_EQUAL(ESP_OK, result);
    // TEST_ASSERT_EQUAL(1, etag_count);
    // TEST_ASSERT_EQUAL_STRING("*", etags[0]);
}
*/

// Test ETag matching
// These tests use internal functions that are not exposed in the public API
/*
void test_etag_matches_success(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 1;
    strncpy(etags[0], "\"test-etag\"", HTTPD_MAX_ETAG_LEN - 1);
    etags[0][HTTPD_MAX_ETAG_LEN - 1] = '\0';
    
    // This function is internal and not exposed in the public API
    // bool result = etag_matches("\"test-etag\"", etags, etag_count);
    // TEST_ASSERT_TRUE(result);
}

void test_etag_matches_failure(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 1;
    strncpy(etags[0], "\"different-etag\"", HTTPD_MAX_ETAG_LEN - 1);
    etags[0][HTTPD_MAX_ETAG_LEN - 1] = '\0';
    
    // This function is internal and not exposed in the public API
    // bool result = etag_matches("\"test-etag\"", etags, etag_count);
    // TEST_ASSERT_FALSE(result);
}

void test_etag_matches_star(void) {
    char etags[10][HTTPD_MAX_ETAG_LEN];
    size_t etag_count = 1;
    strncpy(etags[0], "*", HTTPD_MAX_ETAG_LEN - 1);
    etags[0][HTTPD_MAX_ETAG_LEN - 1] = '\0';
    
    // This function is internal and not exposed in the public API
    // bool result = etag_matches("\"test-etag\"", etags, etag_count);
    // TEST_ASSERT_TRUE(result);
}
*/

// Test middleware with no conditional headers
void test_middleware_no_conditional_headers(void) {
    reset_conditional_mocks();
    
    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;
    
    memset(&req, 0, sizeof(req));
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));
    
    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;
    
    // No conditional headers set
    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_OK, result); // Should continue processing
    
    // Should have set ETag and Last-Modified headers
    const char *etag_header = get_conditional_response_header("ETag");
    TEST_ASSERT_NOT_NULL(etag_header);
    TEST_ASSERT_EQUAL_STRING("\"test-etag\"", etag_header);
    
    const char *last_modified_header = get_conditional_response_header("Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified_header);
}

// Test If-Match with matching ETag
void test_middleware_if_match_matching_etag(void) {
    reset_conditional_mocks();
    
    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;
    
    memset(&req, 0, sizeof(req));
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));
    
    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;
    
    // Set If-Match header with matching ETag
    set_test_conditional_header("If-Match", "\"test-etag\"");
    
    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_OK, result); // Should continue processing
}

// Test If-Match with non-matching ETag
void test_middleware_if_match_non_matching_etag(void) {
    reset_conditional_mocks();
    
    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;
    
    memset(&req, 0, sizeof(req));
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));
    
    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;
    
    // Set If-Match header with non-matching ETag
    set_test_conditional_header("If-Match", "\"different-etag\"");
    
    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing
    
    // Should have sent 412 Precondition Failed
    TEST_ASSERT_EQUAL_STRING("412 Precondition Failed", mock_response_status_str);
}

// Test If-None-Match with matching ETag for GET request
void test_middleware_if_none_match_matching_etag_get(void) {
    reset_conditional_mocks();
    
    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;
    
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET; // GET request
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));
    
    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;
    
    // Set If-None-Match header with matching ETag
    set_test_conditional_header("If-None-Match", "\"test-etag\"");

    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing

    // Should have sent 304 Not Modified
    TEST_ASSERT_EQUAL_STRING("304 Not Modified", mock_response_status_str);

    // Should have set ETag and Last-Modified headers
    const char *etag_header = get_conditional_response_header("ETag");
    TEST_ASSERT_NOT_NULL(etag_header);
    TEST_ASSERT_EQUAL_STRING("\"test-etag\"", etag_header);

    const char *last_modified_header = get_conditional_response_header("Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified_header);
}

// Test If-None-Match with matching ETag for POST request
void test_middleware_if_none_match_matching_etag_post(void) {
    reset_conditional_mocks();

    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST; // POST request
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));

    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;

    // Set If-None-Match header with matching ETag
    set_test_conditional_header("If-None-Match", "\"test-etag\"");
    
    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing
    
    // Should have sent 412 Precondition Failed
    TEST_ASSERT_EQUAL_STRING("412 Precondition Failed", mock_response_status_str);
}

// Test If-Modified-Since with matching timestamp
void test_middleware_if_modified_since_matching(void) {
    reset_conditional_mocks();
    
    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;
    
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET; // GET request
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));
    
    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;
    
    // Set If-Modified-Since header with matching timestamp
    set_test_conditional_header("If-Modified-Since", "Fri, 01 Jan 2021 00:00:00 GMT");

    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing

    // Should have sent 304 Not Modified
    TEST_ASSERT_EQUAL_STRING("304 Not Modified", mock_response_status_str);

    // Should have set Last-Modified header
    const char *last_modified_header = get_conditional_response_header("Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified_header);
}

// Test If-Unmodified-Since with non-matching timestamp
void test_middleware_if_unmodified_since_non_matching(void) {
    reset_conditional_mocks();

    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;

    memset(&req, 0, sizeof(req));
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));

    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;

    // Set If-Unmodified-Since header with non-matching timestamp
    set_test_conditional_header("If-Unmodified-Since", "Wed, 30 Dec 2020 23:59:59 GMT");
    
    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing
    
    // Should have sent 412 Precondition Failed
    TEST_ASSERT_EQUAL_STRING("412 Precondition Failed", mock_response_status_str);
}

// Test date parsing and formatting
void test_parse_http_date_success(void) {
    long long timestamp;
    esp_err_t result = httpd_parse_http_date("Fri, 01 Jan 2021 00:00:00 GMT", &timestamp);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(timestamp > 0);
}

void test_parse_http_date_invalid_args(void) {
    long long timestamp;
    
    // Test with NULL date string
    esp_err_t result = httpd_parse_http_date(NULL, &timestamp);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Test with NULL timestamp pointer
    result = httpd_parse_http_date("Fri, 01 Jan 2021 00:00:00 GMT", NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
}

void test_format_http_date_success(void) {
    char date_str[64];
    long long timestamp = 1609459200; // 2021-01-01 00:00:00 UTC
    esp_err_t result = httpd_format_http_date(timestamp, date_str, sizeof(date_str));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(date_str);
    TEST_ASSERT_TRUE(strlen(date_str) > 0);
}

void test_format_http_date_invalid_args(void) {
    char date_str[64];
    long long timestamp = 1609459200;
    
    // Test with NULL date string
    esp_err_t result = httpd_format_http_date(timestamp, NULL, sizeof(date_str));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Test with insufficient buffer size
    result = httpd_format_http_date(timestamp, date_str, 10);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
}

// Test weak ETag matching for If-None-Match GET requests
void test_middleware_if_none_match_weak_etag_get(void) {
    reset_conditional_mocks();

    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET; // GET request uses weak comparison
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));

    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;

    // Set If-None-Match header with W/"test-etag" (weak ETag)
    set_test_conditional_header("If-None-Match", "W/\"test-etag\"");

    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); // Should stop processing

    // Should have sent 304 Not Modified (weak comparison matches)
    TEST_ASSERT_EQUAL_STRING("304 Not Modified", mock_response_status_str);
}

// Test strong ETag matching for If-None-Match POST requests
void test_middleware_if_none_match_strong_etag_post(void) {
    reset_conditional_mocks();

    struct httpd_req req;
    struct httpd_uri uri;
    httpd_conditional_middleware_config_t config;

    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST; // POST request uses strong comparison
    memset(&uri, 0, sizeof(uri));
    memset(&config, 0, sizeof(config));

    // Set up callbacks for testing
    config.etag_generator = mock_etag_generator;
    config.last_modified_fn = mock_last_modified_fn;
    config.req_get_hdr_value_str = mock_httpd_req_get_hdr_value_str;
    config.req_get_hdr_value_len = mock_req_get_hdr_value_len;
    config.resp_set_status = mock_httpd_resp_set_status;
    config.resp_set_hdr = mock_httpd_resp_set_hdr;
    config.resp_send = mock_httpd_resp_send;

    // Set If-None-Match header with W/"test-etag" (weak ETag) vs strong ETag in resource
    // Strong comparison should fail
    set_test_conditional_header("If-None-Match", "W/\"test-etag\"");

    esp_err_t result = middleware_conditional(&req, &uri, &config);
    TEST_ASSERT_EQUAL(ESP_OK, result); // Should continue processing (no match)
}

// Test If-Range parsing with ETag
void test_if_range_parsing_etag_success(void) {
    httpd_if_range_condition_t condition;
    esp_err_t result = httpd_parse_if_range_header("\"test-etag\"", &condition);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(condition.is_etag);
    TEST_ASSERT_EQUAL_STRING("\"test-etag\"", condition.etag);
}

// Test If-Range parsing with weak ETag
void test_if_range_parsing_weak_etag_success(void) {
    httpd_if_range_condition_t condition;
    esp_err_t result = httpd_parse_if_range_header("W/\"test-etag\"", &condition);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(condition.is_etag);
    TEST_ASSERT_EQUAL_STRING("W/\"test-etag\"", condition.etag);
}

// Test If-Range parsing with HTTP-date
void test_if_range_parsing_http_date_success(void) {
    httpd_if_range_condition_t condition;
    esp_err_t result = httpd_parse_if_range_header("Wed, 31 Dec 2020 23:59:59 GMT", &condition);
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_FALSE(condition.is_etag);
    TEST_ASSERT_TRUE(condition.http_date > 0);
}

// Test If-Range parsing invalid input
void test_if_range_parsing_invalid(void) {
    httpd_if_range_condition_t condition;
    esp_err_t result = httpd_parse_if_range_header("invalid", &condition);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
}

// Test If-Range evaluation with matching ETag (weak comparison)
void test_if_range_evaluation_matching_etag(void) {
    httpd_if_range_condition_t condition = {
        .is_etag = true,
        .etag = "\"test-etag\""
    };

    const char *resource_etag = "\"test-etag\"";
    long long resource_last_modified = 1609459200; // 2021-01-01

    httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(NULL, resource_etag, resource_last_modified, &condition);
    TEST_ASSERT_EQUAL(HTTPD_IF_RANGE_PROCESS_RANGE, result);
}

// Test If-Range evaluation with non-matching ETag
void test_if_range_evaluation_non_matching_etag(void) {
    httpd_if_range_condition_t condition = {
        .is_etag = true,
        .etag = "\"different-etag\""
    };

    const char *resource_etag = "\"test-etag\"";
    long long resource_last_modified = 1609459200;

    httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(NULL, resource_etag, resource_last_modified, &condition);
    TEST_ASSERT_EQUAL(HTTPD_IF_RANGE_IGNORE_RANGE, result);
}

// Test If-Range evaluation with weak ETag matching strong ETag
void test_if_range_evaluation_weak_etag_match(void) {
    httpd_if_range_condition_t condition = {
        .is_etag = true,
        .etag = "W/\"test-etag\""
    };

    const char *resource_etag = "\"test-etag\""; // Strong ETag
    long long resource_last_modified = 1609459200;

    httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(NULL, resource_etag, resource_last_modified, &condition);
    TEST_ASSERT_EQUAL(HTTPD_IF_RANGE_PROCESS_RANGE, result); // Weak comparison matches
}

// Test If-Range evaluation with HTTP-date (resource not modified)
void test_if_range_evaluation_http_date_not_modified(void) {
    httpd_if_range_condition_t condition = {
        .is_etag = false,
        .http_date = 1609459199 // Before resource modification
    };

    const char *resource_etag = NULL;
    long long resource_last_modified = 1609459200; // After condition date

    httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(NULL, resource_etag, resource_last_modified, &condition);
    TEST_ASSERT_EQUAL(HTTPD_IF_RANGE_IGNORE_RANGE, result); // Resource modified after condition
}

// Test If-Range evaluation with HTTP-date (resource not modified)
void test_if_range_evaluation_http_date_not_modified_match(void) {
    httpd_if_range_condition_t condition = {
        .is_etag = false,
        .http_date = 1609459201 // After resource modification
    };

    const char *resource_etag = NULL;
    long long resource_last_modified = 1609459200; // Before condition date

    httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(NULL, resource_etag, resource_last_modified, &condition);
    TEST_ASSERT_EQUAL(HTTPD_IF_RANGE_PROCESS_RANGE, result); // Resource not modified after condition
}

// Main test function
int test_conditional(void) {
    UnitySetTestFile(__FILE__);

    // Test ETag generation functions
    RUN_TEST(test_generate_strong_etag_success);
    RUN_TEST(test_generate_strong_etag_invalid_args);
    RUN_TEST(test_generate_weak_etag_success);
    RUN_TEST(test_generate_weak_etag_invalid_args);

    // Test middleware with no conditional headers
    RUN_TEST(test_middleware_no_conditional_headers);

    // Test If-Match scenarios
    RUN_TEST(test_middleware_if_match_matching_etag);
    RUN_TEST(test_middleware_if_match_non_matching_etag);

    // Test If-None-Match scenarios
    RUN_TEST(test_middleware_if_none_match_matching_etag_get);
    RUN_TEST(test_middleware_if_none_match_matching_etag_post);
    RUN_TEST(test_middleware_if_none_match_weak_etag_get);
    RUN_TEST(test_middleware_if_none_match_strong_etag_post);

    // Test If-Modified-Since scenarios
    RUN_TEST(test_middleware_if_modified_since_matching);

    // Test If-Unmodified-Since scenarios
    RUN_TEST(test_middleware_if_unmodified_since_non_matching);

    // Test If-Range parsing
    RUN_TEST(test_if_range_parsing_etag_success);
    RUN_TEST(test_if_range_parsing_weak_etag_success);
    RUN_TEST(test_if_range_parsing_http_date_success);
    RUN_TEST(test_if_range_parsing_invalid);

    // Test If-Range evaluation
    RUN_TEST(test_if_range_evaluation_matching_etag);
    RUN_TEST(test_if_range_evaluation_non_matching_etag);
    RUN_TEST(test_if_range_evaluation_weak_etag_match);
    RUN_TEST(test_if_range_evaluation_http_date_not_modified);
    RUN_TEST(test_if_range_evaluation_http_date_not_modified_match);

    // Test date parsing and formatting
    RUN_TEST(test_parse_http_date_success);
    RUN_TEST(test_parse_http_date_invalid_args);
    RUN_TEST(test_format_http_date_success);
    RUN_TEST(test_format_http_date_invalid_args);

    return 0;
}
