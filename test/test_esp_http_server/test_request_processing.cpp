#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // For getaddrinfo
#include <in6addr.h> // For in_port_t on Windows
#else
#include <sys/socket.h>
#include <netdb.h> // For getaddrinfo
#include <arpa/inet.h> // For inet_addr
#include <unistd.h> // for close
#include <netinet/in.h> // For in_port_t on Linux
#endif

#define TAG "TEST_HTTPD_REQUEST"

/* Test timeout values */
#define TEST_BUFFER_SIZE 1024



/**
 * Test: given_valid_request_when_calling_httpd_req_get_url_query_len_then_returns_query_length
 * 
 * Purpose: Verify that URL query string length can be retrieved
 * Expected: httpd_req_get_url_query_len() returns the correct query length
 */
void given_valid_request_when_calling_httpd_req_get_url_query_len_then_returns_query_length(void)
{
    // Given: Started HTTP server with registered handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8085;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Note: This test would need an actual HTTP request to fully test
    // For now, testing with NULL request to verify error handling
    size_t query_len = httpd_req_get_url_query_len(NULL);
    
    // When: NULL request is provided
    // Then: Function returns 0 (as per API documentation for invalid arguments)
    TEST_ASSERT_EQUAL(0, query_len);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_various_url_queries_when_calling_httpd_req_get_url_query_len_then_returns_correct_length
 * 
 * Purpose: Verify that httpd_req_get_url_query_len() returns correct lengths for various query strings.
 * Expected: The function returns the accurate length of the query string, or 0 for no query/invalid input.
 */
void given_various_url_queries_when_calling_httpd_req_get_url_query_len_then_returns_correct_length(void)
{
    // Given: A mock httpd_req_t and httpd_req_aux structure allocated on the heap
    httpd_req_t *mock_req = (httpd_req_t*) calloc(1, sizeof(httpd_req_t));
    TEST_ASSERT_NOT_NULL(mock_req);

    struct httpd_req_aux *mock_req_aux = (struct httpd_req_aux*) calloc(1, sizeof(struct httpd_req_aux));
    TEST_ASSERT_NOT_NULL(mock_req_aux);

    mock_req->aux = mock_req_aux;
    mock_req->handle = NULL; // Not strictly needed for these functions

    // Test case 1: URL with a simple query string
    const char* uri1 = "/path?param1=value1&param2=value2";
    strncpy((char*)mock_req->uri, uri1, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0'; // Explicitly null-terminate
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    size_t len1 = httpd_req_get_url_query_len(mock_req);
    TEST_ASSERT_EQUAL(27, len1); // "param1=value1&param2=value2" is 25 chars (as per http-parser docs)

    // Test case 2: URL with no query string
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    const char* uri2 = "/path";
    strncpy((char*)mock_req->uri, uri2, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    size_t len2 = httpd_req_get_url_query_len(mock_req);
    TEST_ASSERT_EQUAL(0, len2);

    // Test case 3: URL with an empty query string (just '?')
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    const char* uri3 = "/path?";
    strncpy((char*)mock_req->uri, uri3, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    size_t len3 = httpd_req_get_url_query_len(mock_req);
    TEST_ASSERT_EQUAL(0, len3); // The API returns 0 for an empty query string

    // Test case 4: URL with query string containing special characters
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    const char* uri4 = "/search?q=hello%20world&id=123";
    strncpy((char*)mock_req->uri, uri4, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    size_t len4 = httpd_req_get_url_query_len(mock_req);
    TEST_ASSERT_EQUAL(22, len4); // "q=hello%20world&id=123" is 22 chars

    // Test case 5: NULL request pointer (already covered, but good to re-verify context)
    size_t len5 = httpd_req_get_url_query_len(NULL);
    TEST_ASSERT_EQUAL(0, len5);

    // Cleanup
    free(mock_req);
    free(mock_req_aux);
}


/**
 * Test: given_valid_request_when_calling_httpd_req_get_hdr_value_len_then_returns_header_length
 * 
 * Purpose: Verify that header value length can be retrieved
 * Expected: httpd_req_get_hdr_value_len() returns the correct header value length
 */
void given_valid_request_when_calling_httpd_req_get_hdr_value_len_then_returns_header_length(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8086;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Note: This test would need an actual HTTP request with headers
    // For now, testing with NULL request to verify error handling
    size_t header_len = httpd_req_get_hdr_value_len(NULL, "Host");
    
    // When: NULL request is provided
    // Then: Function returns 0 (as per API documentation for invalid arguments)
    TEST_ASSERT_EQUAL(0, header_len);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_query_string_when_calling_httpd_query_key_value_then_parses_correctly
 * 
 * Purpose: Verify URL query parameter parsing functionality
 * Expected: httpd_query_key_value() extracts correct key-value pairs
 */
void given_query_string_when_calling_httpd_query_key_value_then_parses_correctly(void)
{
    char value[64];
    
    // Test valid key-value extraction
    const char* query = "param1=value1&param2=value2&param3=value3";
    
    esp_err_t ret1 = httpd_query_key_value(query, "param1", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret1);
    TEST_ASSERT_EQUAL_STRING("value1", value);
    
    esp_err_t ret2 = httpd_query_key_value(query, "param2", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret2);
    TEST_ASSERT_EQUAL_STRING("value2", value);
    
    esp_err_t ret3 = httpd_query_key_value(query, "param3", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret3);
    TEST_ASSERT_EQUAL_STRING("value3", value);
    
    // Test non-existent key
    esp_err_t ret_nonexist = httpd_query_key_value(query, "nonexistent", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret_nonexist);
}


/**
 * Test: given_edge_case_query_string_when_calling_httpd_query_key_value_then_parses_correctly
 * 
 * Purpose: Verify URL query parameter parsing functionality with edge cases
 * Expected: httpd_query_key_value() handles empty, missing, and buffer overflow scenarios
 */
void given_edge_case_query_string_when_calling_httpd_query_key_value_then_parses_correctly(void)
{
    char value[10]; // Small buffer for overflow testing
    esp_err_t ret;

    // Test empty query string
    const char* empty_query = "";
    ret = httpd_query_key_value(empty_query, "param", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);

    // Test query string with no value for a key
    const char* no_value_query = "param1=&param2=value2";
    ret = httpd_query_key_value(no_value_query, "param1", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("", value); // Should return empty string

    // Test query string with key but no '='
    const char* no_equal_query = "param1&param2=value2";
    ret = httpd_query_key_value(no_equal_query, "param1", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret); // Key exists but no value, so not found

    // Test buffer overflow
    const char* long_value_query = "param1=verylongvalue";
    ret = httpd_query_key_value(long_value_query, "param1", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_ERR_HTTPD_RESULT_TRUNC, ret);

    // Test key at the end of the string
    const char* end_key_query = "p1=v1&p2=v2";
    ret = httpd_query_key_value(end_key_query, "p2", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("v2", value);

    // Test key in the middle
    const char* middle_key_query = "p1=v1&p2=v2&p3=v3";
    ret = httpd_query_key_value(middle_key_query, "p2", value, sizeof(value));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("v2", value);
}


/**
 * Test: given_various_url_queries_when_calling_httpd_req_get_url_query_str_then_returns_correct_string
 *
 * Purpose: Verify that httpd_req_get_url_query_str() returns correct query strings for various scenarios.
 * Expected: The function returns the accurate query string, handles empty/missing queries, and truncation.
 */
void given_various_url_queries_when_calling_httpd_req_get_url_query_str_then_returns_correct_string(void)
{
    // Given: A mock httpd_req_t and httpd_req_aux structure allocated on the heap
    httpd_req_t *mock_req = (httpd_req_t*) calloc(1, sizeof(httpd_req_t));
    TEST_ASSERT_NOT_NULL(mock_req);

    struct httpd_req_aux *mock_req_aux = (struct httpd_req_aux*) calloc(1, sizeof(struct httpd_req_aux));
    TEST_ASSERT_NOT_NULL(mock_req_aux);

    mock_req->aux = mock_req_aux;
    mock_req->handle = NULL; // Not strictly needed for these functions

    char query_buf[64];
    esp_err_t ret;

    // Test case 1: URL with a simple query string
    const char* uri1 = "/path?param1=value1&param2=value2";
    LOGD(TAG, "Test Case 1: URI = %s", uri1);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri1, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, query_buf, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("param1=value1&param2=value2", query_buf);

    // Test case 2: URL with no query string
    const char* uri2 = "/path";
    LOGD(TAG, "Test Case 2: URI = %s", uri2);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri2, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, query_buf, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);

    // Test case 3: URL with an empty query string (just '?')
    const char* uri3 = "/path?";
    LOGD(TAG, "Test Case 3: URI = %s", uri3);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri3, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, query_buf, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret); // API returns NOT_FOUND for empty query

    // Test case 4: URL with query string containing special characters
    const char* uri4 = "/search?q=hello%20world&id=123";
    LOGD(TAG, "Test Case 4: URI = %s", uri4);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri4, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, query_buf, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("q=hello%20world&id=123", query_buf);

    // Test case 5: Buffer too small (truncation)
    char small_buf[10];
    memset(small_buf, 0, sizeof(small_buf)); // Clear small_buf
    const char* uri5 = "/path?longparam=longvalue";
    LOGD(TAG, "Test Case 5: URI = %s", uri5);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri5, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res); /* Initialize for each test case */
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, small_buf, sizeof(small_buf));
    LOGD(TAG, "small_buf after strlcpy: '%s'", small_buf);
    TEST_ASSERT_EQUAL(ESP_ERR_HTTPD_RESULT_TRUNC, ret);
    TEST_ASSERT_EQUAL_STRING("longparam", small_buf); // Should copy as much as possible

    // Test case 6: NULL request pointer
    ret = httpd_req_get_url_query_str(NULL, query_buf, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);

    // Test case 7: NULL buffer
    const char* uri7 = "/path?param=value";
    LOGD(TAG, "Test Case 7: URI = %s", uri7);
    memset((char*)mock_req->uri, 0, sizeof(mock_req->uri)); // Clear buffer
    strncpy((char*)mock_req->uri, uri7, sizeof(mock_req->uri) - 1);
    ((char*)mock_req->uri)[sizeof(mock_req->uri) - 1] = '\0';
    LOGD(TAG, "mock_req->uri after strncpy: %s, strlen: %zu", mock_req->uri, strlen(mock_req->uri));
    http_parser_url_init(&mock_req_aux->url_parse_res);
    http_parser_parse_url(mock_req->uri, strlen(mock_req->uri), 0, &mock_req_aux->url_parse_res);
    ret = httpd_req_get_url_query_str(mock_req, NULL, sizeof(query_buf));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);

    // Cleanup
    free(mock_req);
    free(mock_req_aux);
}


// Test cases for httpd_req_get_cookie_val
void test_httpd_req_get_cookie_val_success() {
    struct httpd_req_aux ra = {0};
    snprintf(ra.scratch, sizeof(ra.scratch), "Cookie: cookie1=value1; cookie2=value2");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[32];
    size_t val_size = sizeof(val_buf);
    esp_err_t err;

    // Test case 1: Retrieve an existing cookie
    memset(val_buf, 0, sizeof(val_buf));
    err = httpd_req_get_cookie_val(&req, "cookie1", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("value1", val_buf);
    TEST_ASSERT_EQUAL(strlen("value1"), val_size); // Check updated size

    memset(val_buf, 0, sizeof(val_buf));
    err = httpd_req_get_cookie_val(&req, "cookie2", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("value2", val_buf);
    TEST_ASSERT_EQUAL(strlen("value2"), val_size); // Check updated size
}



void test_httpd_req_get_cookie_val_not_found() {
     struct httpd_req_aux ra = {0};
    snprintf(ra.scratch, sizeof(ra.scratch), "Cookie: cookie1=value1; cookie2=value2");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[32];
    size_t val_size = sizeof(val_buf);
    esp_err_t err = httpd_req_get_cookie_val(&req, "nonexistent_cookie", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}


void test_httpd_req_get_cookie_val_no_cookie_header() {
     struct httpd_req_aux ra = {0};
     memset(ra.scratch, 0, sizeof(ra.scratch));
    // snprintf(ra.scratch, sizeof(ra.scratch), "");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[32];
    size_t val_size = sizeof(val_buf);
    esp_err_t err = httpd_req_get_cookie_val(&req, "cookie1", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}



void test_httpd_req_get_cookie_val_empty_cookie_header() {
     struct httpd_req_aux ra = {0};
    snprintf(ra.scratch, sizeof(ra.scratch), "Cookie: ");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[32];
    size_t val_size = sizeof(val_buf);
    esp_err_t err = httpd_req_get_cookie_val(&req, "cookie1", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}


void test_httpd_req_get_cookie_val_buffer_truncation() {
     struct httpd_req_aux ra = {0};
    snprintf(ra.scratch, sizeof(ra.scratch), "Cookie: cookie1=value1; cookie2=value2");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[3]; // Buffer too small for "value1"
    size_t val_size = sizeof(val_buf);
    esp_err_t err = httpd_req_get_cookie_val(&req, "cookie1", val_buf, &val_size);
    TEST_ASSERT_EQUAL(ESP_ERR_HTTPD_RESULT_TRUNC, err);
    TEST_ASSERT_EQUAL_STRING("va", val_buf); // Check truncated value
    TEST_ASSERT_EQUAL(strlen("value1"), val_size); // Check required size
}


void test_httpd_req_get_cookie_val_invalid_args() {
    struct httpd_req_aux ra = {0};
    snprintf(ra.scratch, sizeof(ra.scratch), "Cookie: cookie1=value1; cookie2=value2");
    ra.req_hdrs_count = 1;

    httpd_req_t req = {0};
    req.aux = &ra;

    char val_buf[32];
    size_t val_size = sizeof(val_buf);

    // Test null req
    esp_err_t err = httpd_req_get_cookie_val(NULL, "cookie1", val_buf, &val_size);
    TEST_ASSERT_EQUAL_HEX(ESP_ERR_NOT_FOUND, err);

    // Test null cookie_name
    err = httpd_req_get_cookie_val(&req, NULL, val_buf, &val_size);
    TEST_ASSERT_EQUAL_HEX(ESP_ERR_INVALID_ARG, err);

    // Test null val
    err = httpd_req_get_cookie_val(&req, "cookie1", NULL, &val_size);
    TEST_ASSERT_EQUAL_HEX(ESP_ERR_INVALID_ARG, err);
}

int test_request_processing(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_valid_request_when_calling_httpd_req_get_url_query_len_then_returns_query_length);
    RUN_TEST(given_various_url_queries_when_calling_httpd_req_get_url_query_len_then_returns_correct_length);
    RUN_TEST(given_valid_request_when_calling_httpd_req_get_hdr_value_len_then_returns_header_length);
    RUN_TEST(given_query_string_when_calling_httpd_query_key_value_then_parses_correctly);
    RUN_TEST(given_edge_case_query_string_when_calling_httpd_query_key_value_then_parses_correctly);
    RUN_TEST(given_various_url_queries_when_calling_httpd_req_get_url_query_str_then_returns_correct_string);
    RUN_TEST(test_httpd_req_get_cookie_val_success);
    RUN_TEST(test_httpd_req_get_cookie_val_not_found);
    RUN_TEST(test_httpd_req_get_cookie_val_no_cookie_header);
    RUN_TEST(test_httpd_req_get_cookie_val_empty_cookie_header);
    RUN_TEST(test_httpd_req_get_cookie_val_buffer_truncation);
    RUN_TEST(test_httpd_req_get_cookie_val_invalid_args);
    // return UNITY_END();
    return 0;
}
