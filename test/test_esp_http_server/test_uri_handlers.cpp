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

#define TAG "TEST_HTTPD_HANDLERS"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define TEST_BUFFER_SIZE 1024


/**
 * Test: given_server_started_when_registering_valid_uri_handler_then_returns_success
 * 
 * Purpose: Verify that URI handlers can be registered successfully
 * Expected: httpd_register_uri_handler() returns ESP_OK
 */
void given_server_started_when_registering_valid_uri_handler_then_returns_success(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8082;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Valid URI handler structure
    httpd_uri_t test_handler = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    // When: URI handler is registered
    esp_err_t ret = httpd_register_uri_handler(handle, &test_handler);
    
    // Then: Registration succeeds
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_null_handler_when_registering_uri_handler_then_returns_invalid_arg
 * 
 * Purpose: Verify error handling when null handler is provided
 * Expected: httpd_register_uri_handler() returns ESP_ERR_INVALID_ARG
 */
void given_null_handler_when_registering_uri_handler_then_returns_invalid_arg(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8083;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // When: NULL handler is registered
    esp_err_t ret = httpd_register_uri_handler(handle, NULL);
    
    // Then: Function returns ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Cleanup
    httpd_stop(handle);
}



/**
 * Test: given_registered_uri_handler_when_unregistering_same_handler_then_returns_success
 * 
 * Purpose: Verify that URI handlers can be unregistered successfully
 * Expected: httpd_unregister_uri_handler() returns ESP_OK
 */
void given_registered_uri_handler_when_unregistering_same_handler_then_returns_success(void)
{
    // Given: Started HTTP server with registered handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8084;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    httpd_uri_t test_handler = {
        .uri = "/test_unregister",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    esp_err_t register_ret = httpd_register_uri_handler(handle, &test_handler);
    TEST_ASSERT_EQUAL(ESP_OK, register_ret);
    
    // When: URI handler is unregistered
    esp_err_t unregister_ret = httpd_unregister_uri_handler(handle, "/test_unregister", HTTP_GET);
    
    // Then: Unregistration succeeds
    TEST_ASSERT_EQUAL(ESP_OK, unregister_ret);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_server_with_max_handlers_when_exceeding_limit_then_handlers_full_error
 * 
 * Purpose: Verify that server enforces maximum URI handler limit
 * Expected: httpd_register_uri_handler() returns ESP_ERR_HTTPD_HANDLERS_FULL
 */
void given_server_with_max_handlers_when_exceeding_limit_then_handlers_full_error(void)
{
    // Given: Server with small max_uri_handlers limit
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2;  // Very low limit for testing
    config.server_port = 8088;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Register handlers up to the limit
    httpd_uri_t handler1 = {
        .uri = "/handler1",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    httpd_uri_t handler2 = {
        .uri = "/handler2", 
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &handler1));
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &handler2));
    
    // When: Attempting to register third handler
    httpd_uri_t handler3 = {
        .uri = "/handler3",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    esp_err_t ret = httpd_register_uri_handler(handle, &handler3);
    
    // Then: Function returns ESP_ERR_HTTPD_HANDLERS_FULL
    TEST_ASSERT_EQUAL(ESP_ERR_HTTPD_HANDLERS_FULL, ret);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_duplicate_handler_registration_when_attempting_then_returns_handler_exists_error
 * 
 * Purpose: Verify that duplicate URI handler registration is prevented
 * Expected: httpd_register_uri_handler() returns ESP_ERR_HTTPD_HANDLER_EXISTS
 */
void given_duplicate_handler_registration_when_attempting_then_returns_handler_exists_error(void)
{
    // Given: Started server with registered handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8089;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    httpd_uri_t handler = {
        .uri = "/duplicate_test",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };
    
    // Register handler first time
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &handler));
    
    // When: Attempting to register same URI+method combination
    esp_err_t ret = httpd_register_uri_handler(handle, &handler);
    
    // Then: Function returns ESP_ERR_HTTPD_HANDLER_EXISTS
    TEST_ASSERT_EQUAL(ESP_ERR_HTTPD_HANDLER_EXISTS, ret);
    
    // Cleanup
    httpd_stop(handle);
}



/**
 * Test: given_multiple_handlers_for_same_uri_when_unregistering_uri_then_all_handlers_are_removed
 *
 * Purpose: Verify that httpd_unregister_uri() successfully removes all handlers associated with a specific URI.
 * Expected: After calling httpd_unregister_uri(), subsequent attempts to unregister individual handlers for that URI return ESP_ERR_NOT_FOUND.
 */
void given_multiple_handlers_for_same_uri_when_unregistering_uri_then_all_handlers_are_removed(void)
{
    // Given: Started HTTP server with multiple handlers for the same URI
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8095; // Use a different port
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);

    httpd_uri_t get_handler = {
        .uri = "/multi_method_test",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };

    httpd_uri_t post_handler = {
        .uri = "/multi_method_test",
        .method = HTTP_POST,
        .handler = [](httpd_req_t *req) { return ESP_OK; },
        .user_ctx = NULL
    };

    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &get_handler));
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &post_handler));

    // When: httpd_unregister_uri is called for the URI
    esp_err_t unregister_all_ret = httpd_unregister_uri(handle, "/multi_method_test");
    TEST_ASSERT_EQUAL(ESP_OK, unregister_all_ret);

    // Then: Both GET and POST handlers for that URI should be unregistered
    esp_err_t get_unregister_ret = httpd_unregister_uri_handler(handle, "/multi_method_test", HTTP_GET);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, get_unregister_ret);

    esp_err_t post_unregister_ret = httpd_unregister_uri_handler(handle, "/multi_method_test", HTTP_POST);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, post_unregister_ret);

    // Cleanup
    httpd_stop(handle);
}

int test_uri_handlers(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_started_when_registering_valid_uri_handler_then_returns_success);
    RUN_TEST(given_null_handler_when_registering_uri_handler_then_returns_invalid_arg);
    RUN_TEST(given_registered_uri_handler_when_unregistering_same_handler_then_returns_success);
    RUN_TEST(given_server_with_max_handlers_when_exceeding_limit_then_handlers_full_error);
    RUN_TEST(given_duplicate_handler_registration_when_attempting_then_returns_handler_exists_error);
    RUN_TEST(given_multiple_handlers_for_same_uri_when_unregistering_uri_then_all_handlers_are_removed);
    // return UNITY_END();
    return 0;
}
