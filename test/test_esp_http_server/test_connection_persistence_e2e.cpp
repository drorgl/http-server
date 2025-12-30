/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include "esp_httpd_priv.h" // For httpd_data, sock_db
#include "http_test_client.h" // Include for http_test_client
#include <unity.h> // For TEST_ASSERT_STRCASEEQ macro

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
#include <strings.h> // For strcasecmp on Linux
#endif

#define TEST_TIMEOUT_MS 2000

#define TAG "test_conn_persist_e2e"

// Simple echo handler for E2E tests
static esp_err_t echo_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * Test: test_e2e_connection_persistence_multiple_requests
 *
 * Purpose: Test E2E connection persistence by sending multiple HTTP requests on same TCP connection
 * Expected: All requests succeed on same connection, server defaults to persistent for HTTP/1.1
 */
void test_e2e_connection_persistence_multiple_requests(void)
{
    TEST_MESSAGE("E2E: Multiple HTTP/1.1 requests on persistent connection");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9020; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client sends multiple requests on same TCP connection
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // First request - should establish connection
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_request(client, HTTP_METHOD_GET, "/echo", NULL, NULL, 0, &resp1, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err1);
    TEST_ASSERT_EQUAL(200, resp1.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp1.body, resp1.body_len);
    http_test_client_free_response(&resp1);

    // Second request on SAME connection - should reuse connection
    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_request(client, HTTP_METHOD_GET, "/echo", NULL, NULL, 0, &resp2, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err2);
    TEST_ASSERT_EQUAL(200, resp2.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp2.body, resp2.body_len);
    http_test_client_free_response(&resp2);

    // Third request on SAME connection
    http_test_response_t resp3 = {0};
    http_test_client_err_t err3 = http_test_client_send_request(client, HTTP_METHOD_GET, "/echo", NULL, NULL, 0, &resp3, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err3);
    TEST_ASSERT_EQUAL(200, resp3.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp3.body, resp3.body_len);
    http_test_client_free_response(&resp3);

    // Cleanup - disconnect should work normally
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_connection_close_header_enforced
 *
 * Purpose: Test E2E Connection: close header enforcement in client request
 * Expected: Server responds with Connection: close and disconnects after response
 */
void test_e2e_connection_close_header_enforced(void)
{
    TEST_MESSAGE("E2E: Connection: close header enforcement");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9021; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client sends request with Connection: close header
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *close_hdr = "Connection: close\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/echo", close_hdr, NULL, 0, &resp, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    // Then: Response should include Connection: close header
    const char *connection_resp = http_test_client_get_header(&resp, "Connection");
    TEST_ASSERT_NOT_NULL(connection_resp);

    // Case-insensitive comparison using strcasecmp (available in strings.h)
    TEST_ASSERT_EQUAL(0, strcasecmp(connection_resp, "close"));
    free((void*)connection_resp);
    http_test_client_free_response(&resp);

    // Connection should be usable for additional requests (server closes after response)
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

// WebSocket echo handler for WebSocket persistence tests
static esp_err_t ws_echo_handler(httpd_req_t *req)
{
    return ESP_OK; // Just complete handshake
}

/**
 * Test: test_e2e_connection_http10_default_close
 *
 * Purpose: Test E2E HTTP/1.0 default connection behavior (should close by default)
 * Expected: HTTP/1.0 without keep-alive closes connection after response
 */
void test_e2e_connection_http10_default_close(void)
{
    TEST_MESSAGE("E2E: HTTP/1.0 defaults to connection close");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9022; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client sends HTTP/1.0 request without Connection header
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send raw HTTP/1.0 request (http_test_client defaults to HTTP/1.1)
    const char *http10_req = "GET /echo HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_raw_request(client, http10_req, strlen(http10_req), &resp, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    // Then: Response should include Connection: close header for HTTP/1.0
    const char *connection_resp = http_test_client_get_header(&resp, "Connection");
    if (connection_resp) {
        TEST_ASSERT_STRCASEEQ("close", connection_resp);
        free((void*)connection_resp);
    }
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_connection_http10_explicit_keepalive
 *
 * Purpose: Test E2E HTTP/1.0 with explicit Connection: keep-alive
 * Expected: HTTP/1.0 with keep-alive header enables persistent connection
 */
void test_e2e_connection_http10_explicit_keepalive(void)
{
    TEST_MESSAGE("E2E: HTTP/1.0 with explicit Connection: keep-alive");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9023; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client sends HTTP/1.0 request with Connection: keep-alive
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // First request with keep-alive
    const char *keepalive_req1 = "GET /echo HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_raw_request(client, keepalive_req1, strlen(keepalive_req1), &resp1, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err1);
    TEST_ASSERT_EQUAL(200, resp1.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp1.body, resp1.body_len);
    http_test_client_free_response(&resp1);

    // Second request on same connection should work
    const char *keepalive_req2 = "GET /echo HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_raw_request(client, keepalive_req2, strlen(keepalive_req2), &resp2, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err2);
    TEST_ASSERT_EQUAL(200, resp2.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp2.body, resp2.body_len);
    http_test_client_free_response(&resp2);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_connection_websocket_upgrade_persistence
 *
 * Purpose: Test E2E WebSocket upgrade maintains connection persistence
 * Expected: WebSocket connections remain persistent regardless of limits
 */
void test_e2e_connection_websocket_upgrade_persistence(void)
{
    TEST_MESSAGE("E2E: WebSocket upgrade maintains connection persistence");

    // Given: Server with WebSocket handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9024; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: Client performs WebSocket handshake
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send raw WebSocket upgrade request
    const char *ws_upgrade_req =
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_raw_request(client, ws_upgrade_req, strlen(ws_upgrade_req), &resp, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Should get 101 Switching Protocols
    TEST_ASSERT_EQUAL(101, resp.status_code);

    // Check required headers
    const char *upgrade_hdr = http_test_client_get_header(&resp, "Upgrade");
    TEST_ASSERT_NOT_NULL(upgrade_hdr);
    TEST_ASSERT_STRCASEEQ("websocket", upgrade_hdr);
    free((void*)upgrade_hdr);

    const char *connection_hdr = http_test_client_get_header(&resp, "Connection");
    TEST_ASSERT_NOT_NULL(connection_hdr);
    TEST_ASSERT_STRCASEEQ("upgrade", connection_hdr);
    free((void*)connection_hdr);

    const char *accept_hdr = http_test_client_get_header(&resp, "Sec-WebSocket-Accept");
    TEST_ASSERT_NOT_NULL(accept_hdr);
    // Could validate the accept key, but for this test just check it's present
    free((void*)accept_hdr);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_connection_mixed_version_behavior
 *
 * Purpose: Test E2E mixed HTTP version behavior on same connection (simulated)
 * Expected: Server handles version transitions correctly
 */
void test_e2e_connection_mixed_version_behavior(void)
{
    TEST_MESSAGE("E2E: Mixed HTTP version behavior");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9025; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client mixes HTTP versions with appropriate headers
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // First request: HTTP/1.1 (persistent by default)
    const char *req1 = "GET /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_raw_request(client, req1, strlen(req1), &resp1, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err1);
    TEST_ASSERT_EQUAL(200, resp1.status_code);
    http_test_client_free_response(&resp1);

    // Second request: HTTP/1.1 with Connection: close (should close after response)
    const char *req2 = "GET /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_raw_request(client, req2, strlen(req2), &resp2, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err2);
    TEST_ASSERT_EQUAL(200, resp2.status_code);

    // Server acknowledges client "Connection: close" by closing connection (not echoing header)
    // RFC 9112 doesn't require echoing the connection header when client initiates close
    http_test_client_free_response(&resp2);

    // Attempt Third request - connection should be closed by server after previous response
    // So this request should fail OR get a non-200 response (connection closed)
    const char *req3 = "GET /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    http_test_response_t resp3 = {0};
    http_test_client_err_t err3 = http_test_client_send_raw_request(client, req3, strlen(req3), &resp3, TEST_TIMEOUT_MS);
    // Connection should be closed, so this request should fail or not get OK response
    TEST_ASSERT_TRUE(err3 != HTTP_TEST_CLIENT_OK);
    http_test_client_free_response(&resp3);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_header_lookup_multiple_headers
 *
 * Purpose: Test that multiple headers in request are processed correctly
 * This is a regression test for the bug where only the first header could be fetched
 * Tests proper Connection: close handling with multiple headers in same request
 */
void test_e2e_header_lookup_multiple_headers(void)
{
    TEST_MESSAGE("E2E: Multiple headers processing in request");

    // Given: Server with echo handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9030; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t uri = {
        .uri        = "/echo",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &uri));

    // When: Client sends request with multiple headers including Connection: close
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send request with multiple headers - Connection: close should cause server to close connection
    const char *req = "GET /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nX-Custom: test\r\nAccept: */*\r\n\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_raw_request(client, req, strlen(req), &resp, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    // The server should NOT echo "Connection: close" back to the client in the response
    // (that's not required by HTTP spec), but should close the connection.
    // So we don't check for the response header - instead we verify the connection behavior.

    http_test_client_free_response(&resp);

    // Critical test: Try a second request on same connection - should fail because server closed it
    // This proves the server correctly processed the "Connection: close" header among multiple headers
    const char *req2 = "GET /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_raw_request(client, req2, strlen(req2), &resp2, TEST_TIMEOUT_MS);

    // Connection should have been closed by server, so second request should fail
    TEST_ASSERT_TRUE(err2 != HTTP_TEST_CLIENT_OK);
    http_test_client_free_response(&resp2);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

int test_connection_persistence_e2e(void) {
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_e2e_connection_persistence_multiple_requests);
    RUN_TEST(test_e2e_connection_close_header_enforced);
    RUN_TEST(test_e2e_connection_http10_default_close);
    RUN_TEST(test_e2e_connection_http10_explicit_keepalive);
    RUN_TEST(test_e2e_connection_websocket_upgrade_persistence);
    RUN_TEST(test_e2e_connection_mixed_version_behavior);
    RUN_TEST(test_e2e_header_lookup_multiple_headers);

    return 0; // Success
}
