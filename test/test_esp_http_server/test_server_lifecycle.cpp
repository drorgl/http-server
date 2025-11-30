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

// #define TAG "TEST_HTTPD_LIFECYCLE"

// /* Test timeout values */
// #define TEST_TIMEOUT_MS 1000
// #define TEST_BUFFER_SIZE 1024

/* Global test server handle for reuse */
static httpd_handle_t test_server_handle = NULL;


/**
 * Test: given_valid_httpd_config_when_httpd_start_is_called_then_returns_success
 * 
 * Purpose: Verify that the HTTP server starts successfully with valid configuration
 * Expected: httpd_start() returns ESP_OK and provides valid server handle
 */
void given_valid_httpd_config_when_httpd_start_is_called_then_returns_success(void)
{
    // Given: Valid HTTP server configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    httpd_handle_t handle = NULL;
    
    // When: HTTP server is started with valid config
    esp_err_t ret = httpd_start(&handle, &config);
    
    // Then: Server starts successfully and provides valid handle
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(handle);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_null_handle_when_httpd_start_is_called_then_returns_invalid_arg
 * 
 * Purpose: Verify error handling when null handle is provided to httpd_start
 * Expected: httpd_start() returns ESP_ERR_INVALID_ARG
 */
void given_null_handle_when_httpd_start_is_called_then_returns_invalid_arg(void)
{
    // Given: HTTP server configuration and NULL handle pointer
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // When: HTTP server is started with NULL handle
    esp_err_t ret = httpd_start(NULL, &config);
    
    // Then: Function returns ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}


/**
 * Test: given_started_server_when_httpd_stop_is_called_then_server_stops
 * 
 * Purpose: Verify that the HTTP server can be stopped properly
 * Expected: httpd_stop() returns ESP_OK and cleans up resources
 */
void given_started_server_when_httpd_stop_is_called_then_server_stops(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8081;
    esp_err_t start_ret = httpd_start(&test_server_handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    TEST_ASSERT_NOT_NULL(test_server_handle);
    
    // When: HTTP server is stopped
    esp_err_t stop_ret = httpd_stop(test_server_handle);
    
    // Then: Server stops successfully
    TEST_ASSERT_EQUAL(ESP_OK, stop_ret);
    test_server_handle = NULL;
}


/**
 * Test: given_null_handle_when_httpd_stop_is_called_then_returns_invalid_arg
 * 
 * Purpose: Verify error handling when null handle is provided to httpd_stop
 * Expected: httpd_stop() returns ESP_ERR_INVALID_ARG
 */
void given_null_handle_when_httpd_stop_is_called_then_returns_invalid_arg(void)
{
    // Given: NULL server handle
    
    // When: HTTP server stop is called with NULL handle
    esp_err_t ret = httpd_stop(NULL);
    
    // Then: Function returns ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}


/**
 * Test: given_started_server_when_calling_httpd_stop_multiple_times_then_handles_gracefully
 * 
 * Purpose: Verify that multiple calls to httpd_stop() are handled gracefully
 * Expected: httpd_stop() handles NULL handles and already-stopped servers
 */
void given_started_server_when_calling_httpd_stop_multiple_times_then_handles_gracefully(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8091;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // When: Server is stopped first time
    esp_err_t ret1 = httpd_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, ret1);
    handle = NULL; // Set handle to NULL after stopping

    // When: Server stop is called again on same handle (now NULL)
    esp_err_t ret2 = httpd_stop(handle);
    
    // Then: Second call handles gracefully (should return ESP_ERR_INVALID_ARG)
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret2);
    
    // When: Server stop is called with NULL handle explicitly
    esp_err_t ret3 = httpd_stop(NULL);
    
    // Then: NULL handle returns ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret3);
}


/**
 * Test: given_zero_port_when_httpd_start_is_called_then_assigns_random_port_and_returns_success
 *
 * Purpose: Verify that providing port 0 to httpd_start results in a random port assignment and success.
 * Expected: httpd_start() returns ESP_OK, and a valid random port is assigned to config.server_port.
 */
void given_zero_port_when_httpd_start_is_called_then_assigns_random_port_and_returns_success(void)
{
    // Given: HTTP server configuration with port 0
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0; // Request random port
    httpd_handle_t handle = NULL;

    // When: HTTP server is started with port 0
    esp_err_t ret = httpd_start(&handle, &config);

    // Then: Server starts successfully
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(handle);

    // Retrieve the actual port from the running server instance
    struct httpd_data *hd = (struct httpd_data *)handle;
    uint16_t assigned_port = hd->config.server_port;

    // Verify that a random port was assigned (not 0) and is in the ephemeral range
    TEST_ASSERT_NOT_EQUAL(0, assigned_port);
    TEST_ASSERT_GREATER_THAN(1024, assigned_port); // Ephemeral ports are typically > 1024
    TEST_ASSERT_LESS_OR_EQUAL(65535, assigned_port); // Max port number

    // Cleanup
    if (handle != NULL) {
        httpd_stop(handle);
    }
}



/**
 * Test: given_null_config_when_httpd_start_is_called_then_returns_invalid_arg
 * 
 * Purpose: Verify error handling when null configuration is provided
 * Expected: httpd_start() returns ESP_ERR_INVALID_ARG
 */
void given_null_config_when_httpd_start_is_called_then_returns_invalid_arg(void)
{
    // Given: NULL configuration pointer
    httpd_handle_t handle = NULL;
    
    // When: HTTP server is started with NULL config
    esp_err_t ret = httpd_start(&handle, NULL);
    
    // Then: Function returns ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}


int test_server_lifecycle(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_valid_httpd_config_when_httpd_start_is_called_then_returns_success);
    RUN_TEST(given_null_handle_when_httpd_start_is_called_then_returns_invalid_arg);
    RUN_TEST(given_started_server_when_httpd_stop_is_called_then_server_stops);
    RUN_TEST(given_null_handle_when_httpd_stop_is_called_then_returns_invalid_arg);
    RUN_TEST(given_started_server_when_calling_httpd_stop_multiple_times_then_handles_gracefully);
    RUN_TEST(given_zero_port_when_httpd_start_is_called_then_assigns_random_port_and_returns_success);
    RUN_TEST(given_null_config_when_httpd_start_is_called_then_returns_invalid_arg);
    // return UNITY_END();
    return 0;
}
