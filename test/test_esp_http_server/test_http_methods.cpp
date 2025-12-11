/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client
#include "test_http_methods.h" // Include our own header

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

#include <errno.h>
#include <sys/time.h> // Required for gettimeofday and struct timeval

#define TAG "TEST_HTTP_METHODS"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000


/**
 * Test: given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly
 *
 * Purpose: Verify that PUT requests with body data are handled correctly.
 * Tests PUT method implementation with request body processing and response validation.
 * This addresses the RFC 1945 compliance gap identified in standards.md.
 */
void given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly(void)
{
    // Given: A running server with a PUT handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9051; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t put_uri = {
        .uri      = "/test_put",
        .method   = HTTP_PUT,
        .handler  = [](httpd_req_t *req) {
            // Read the PUT body data
            char buf[128];
            int total_read = 0;
            int ret;

            while ((ret = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
                total_read += ret;
            }

            if (total_read > 0) {
                // Echo back the data with a success message
                char response[256];
                snprintf(response, sizeof(response), "PUT received %d bytes: %.*s", total_read, total_read, buf);
                httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
            } else {
                httpd_resp_send(req, "PUT received with no body", HTTPD_RESP_USE_STRLEN);
            }
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &put_uri));

    // When: A client connects and sends a PUT request with body data
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *put_data = "Hello from PUT request";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client,
                                                                           HTTP_METHOD_PUT,
                                                                           "/test_put",
                                                                           NULL,
                                                                           put_data, strlen(put_data),
                                                                           &response, TEST_TIMEOUT_MS));

    // Then: The server should handle the PUT request correctly
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "PUT received"));
    TEST_ASSERT_NOT_NULL(strstr(response.body, put_data));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly
 *
 * Purpose: Verify that DELETE requests are handled correctly without expecting a body.
 * Tests DELETE method implementation and response validation.
 * This addresses the RFC 1945 compliance gap identified in standards.md.
 */
void given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly(void)
{
    // Given: A running server with a DELETE handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9052; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t delete_uri = {
        .uri      = "/test_delete",
        .method   = HTTP_DELETE,
        .handler  = [](httpd_req_t *req) {
            // DELETE typically doesn't have a body, just acknowledge the request
            httpd_resp_send(req, "Resource deleted successfully", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &delete_uri));

    // When: A client connects and sends a DELETE request
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client,
                                                                           HTTP_METHOD_DELETE,
                                                                           "/test_delete",
                                                                           NULL,
                                                                           NULL, 0,
                                                                           &response, TEST_TIMEOUT_MS));

    // Then: The server should handle the DELETE request correctly
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "deleted successfully"));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only
 *
 * Purpose: Verify that HEAD requests return only headers without body data.
 * Tests HEAD method implementation and validates that no body is returned.
 * This addresses the RFC 1945 compliance gap identified in standards.md.
 */
void given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only(void)
{
    // Given: A running server with a HEAD handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9053; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t head_uri = {
        .uri      = "/test_head",
        .method   = HTTP_HEAD,
        .handler  = [](httpd_req_t *req) {
            // Set some response headers like a normal GET would
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_set_hdr(req, "X-Custom-Header", "Head-Request-Test");
            // HEAD responses should have headers but NO body (different from GET)
            // Send response without body
            httpd_resp_send(req, "", 0);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &head_uri));

    // When: A client connects and sends a HEAD request
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client,
                                                                           HTTP_METHOD_HEAD,
                                                                           "/test_head",
                                                                           NULL,
                                                                           NULL, 0,
                                                                           &response, TEST_TIMEOUT_MS));

    // Then: The server should return headers but no body for HEAD request
    TEST_ASSERT_EQUAL(200, response.status_code);

    // Check that appropriate headers are present
    TEST_ASSERT_NOT_NULL(response.headers);
    TEST_ASSERT_NOT_NULL(strstr(response.headers, "Content-Type: text/plain"));
    TEST_ASSERT_NOT_NULL(strstr(response.headers, "X-Custom-Header: Head-Request-Test"));

    // HEAD responses should not have a body (or body length should be 0)
    TEST_ASSERT_TRUE(response.body_len == 0 || response.body == NULL);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_get_only_handler_when_client_sends_put_delete_head_then_405_method_not_allowed
 *
 * Purpose: Verify that servers return 405 Method Not Allowed for unsupported methods.
 * Tests method validation and ensures proper error responses for PUT/DELETE/HEAD on GET-only endpoints.
 * This validates the method matching logic works correctly for all HTTP methods.
 */
void given_server_with_get_only_handler_when_client_sends_put_delete_head_then_405_method_not_allowed(void)
{
    // Given: A running server with only a GET handler for a URI
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9054; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t get_uri = {
        .uri      = "/test_method_validation",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_send(req, "GET method allowed", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &get_uri));

    // Test PUT method not allowed
    {
        http_test_client_handle_t *put_client = http_test_client_init();
        TEST_ASSERT_NOT_NULL(put_client);
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(put_client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

        http_test_response_t put_response = {0};
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(put_client,
                                                                               HTTP_METHOD_PUT,
                                                                               "/test_method_validation",
                                                                               NULL,
                                                                               "some data", 9,
                                                                               &put_response, TEST_TIMEOUT_MS));
        TEST_ASSERT_EQUAL(405, put_response.status_code);
        TEST_ASSERT_NOT_NULL(put_response.status_text);
        TEST_ASSERT_EQUAL_STRING("Method Not Allowed", put_response.status_text);
        http_test_client_free_response(&put_response);
        http_test_client_disconnect(put_client);
    }

    // Test DELETE method not allowed
    {
        http_test_client_handle_t *delete_client = http_test_client_init();
        TEST_ASSERT_NOT_NULL(delete_client);
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(delete_client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

        http_test_response_t delete_response = {0};
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(delete_client,
                                                                               HTTP_METHOD_DELETE,
                                                                               "/test_method_validation",
                                                                               NULL,
                                                                               NULL, 0,
                                                                               &delete_response, TEST_TIMEOUT_MS));
        TEST_ASSERT_EQUAL(405, delete_response.status_code);
        TEST_ASSERT_NOT_NULL(delete_response.status_text);
        TEST_ASSERT_EQUAL_STRING("Method Not Allowed", delete_response.status_text);
        http_test_client_free_response(&delete_response);
        http_test_client_disconnect(delete_client);
    }

    // Test HEAD method not allowed
    {
        http_test_client_handle_t *head_client = http_test_client_init();
        TEST_ASSERT_NOT_NULL(head_client);
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(head_client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

        http_test_response_t head_response = {0};
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(head_client,
                                                                               HTTP_METHOD_HEAD,
                                                                               "/test_method_validation",
                                                                               NULL,
                                                                               NULL, 0,
                                                                               &head_response, TEST_TIMEOUT_MS));
        TEST_ASSERT_EQUAL(405, head_response.status_code);
        TEST_ASSERT_NOT_NULL(head_response.status_text);
        TEST_ASSERT_EQUAL_STRING("Method Not Allowed", head_response.status_text);
        http_test_client_free_response(&head_response);
        http_test_client_disconnect(head_client);
    }

    // Verify GET still works
    {
        http_test_client_handle_t *get_client = http_test_client_init();
        TEST_ASSERT_NOT_NULL(get_client);
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(get_client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

        http_test_response_t get_response = {0};
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(get_client,
                                                                               HTTP_METHOD_GET,
                                                                               "/test_method_validation",
                                                                               NULL,
                                                                               NULL, 0,
                                                                               &get_response, TEST_TIMEOUT_MS));
        TEST_ASSERT_EQUAL(200, get_response.status_code);
        TEST_ASSERT_NOT_NULL(get_response.body);
        TEST_ASSERT_NOT_NULL(strstr(get_response.body, "GET method allowed"));
        http_test_client_free_response(&get_response);
        http_test_client_disconnect(get_client);
    }

    // Cleanup
    httpd_stop(handle);
}

int test_http_methods(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly);
    RUN_TEST(given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly);
    RUN_TEST(given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only);
    RUN_TEST(given_server_with_get_only_handler_when_client_sends_put_delete_head_then_405_method_not_allowed);
    // return UNITY_END();
    return 0;
}
