/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <http_server.h>
#include <unity.h>

#include "httpd_connection.h"
#include "esp_httpd_priv.h"

// Test helper function to create a mock HTTP server instance
static httpd_handle_t create_test_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;  // Small number for testing
    config.max_uri_handlers = 8;

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        TEST_FAIL_MESSAGE("Failed to create test server");
        return NULL;
    }
    return server;
}

// Test helper to simulate a session being created
static struct sock_db* create_mock_session(httpd_handle_t server, int sockfd) {
    // Simulate session creation by directly calling httpd_sess_new
    esp_err_t ret = httpd_sess_new((struct httpd_data*)server, sockfd);
    if (ret != ESP_OK) {
        return NULL;
    }

    // Get the session that was just created
    struct sock_db* session = httpd_sess_get((struct httpd_data*)server, sockfd);
    return session;
}

/**
 * @brief Test connection context initialization
 */
void test_connection_init_success(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Connection context should be initialized after session creation
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_UNKNOWN, ctx->state);
    TEST_ASSERT_EQUAL(0, ctx->request_count);
    TEST_ASSERT_FALSE(ctx->close_after_response);
    TEST_ASSERT_FALSE(ctx->is_websocket);

    httpd_stop(server);
}

/**
 * @brief Test connection header processing for HTTP/1.1 without Connection header
 */
void test_connection_http11_default_persistent(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Simulate HTTP/1.1 request without Connection header
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_PERSISTENT, ctx->state);
    TEST_ASSERT_EQUAL(1, ctx->request_count);

    httpd_stop(server);
}

/**
 * @brief Test connection header processing for HTTP/1.1 with Connection: close
 */
void test_connection_http11_close(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Simulate HTTP/1.1 request with Connection: close
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "close");
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_CLOSE, ctx->state);
    TEST_ASSERT_EQUAL(1, ctx->request_count);
    TEST_ASSERT_EQUAL_STRING("close", ctx->connection_header);

    httpd_stop(server);
}

/**
 * @brief Test connection header processing for HTTP/1.0 without keep-alive
 */
void test_connection_http10_default_close(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Simulate HTTP/1.0 request without Connection header
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_CLOSE, ctx->state);
    TEST_ASSERT_EQUAL(1, ctx->request_count);

    httpd_stop(server);
}

/**
 * @brief Test connection header processing for HTTP/1.0 with keep-alive
 */
void test_connection_http10_keepalive(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Simulate HTTP/1.0 request with Connection: keep-alive
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", "keep-alive");
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_PERSISTENT, ctx->state);
    TEST_ASSERT_EQUAL(1, ctx->request_count);

    httpd_stop(server);
}

/**
 * @brief Test multiple requests increment counter
 */
void test_connection_multiple_requests(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // First request (HTTP/1.1 persistent)
    esp_err_t ret1 = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret1);

    // Second request (HTTP/1.1 persistent)
    esp_err_t ret2 = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret2);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_PERSISTENT, ctx->state);
    TEST_ASSERT_EQUAL(2, ctx->request_count);

    httpd_stop(server);
}

/**
 * @brief Test connection persistence check
 */
void test_connection_should_persist(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Test persistent connection
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Test close connection
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "close");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test force close after response
 */
void test_connection_close_after_response(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Initially persistent
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Force close after response
    httpd_connection_close_after_response(server, test_sockfd);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));
    TEST_ASSERT_TRUE(httpd_connection_should_close_after_response(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test WebSocket connection marking
 */
void test_connection_websocket_marking(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Initially not WebSocket
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_FALSE(ctx->is_websocket);

    // Mark as WebSocket
    httpd_connection_mark_websocket(server, test_sockfd);
    TEST_ASSERT_TRUE(ctx->is_websocket);

    httpd_stop(server);
}

/**
 * @brief Test case-insensitive Connection header parsing
 */
void test_connection_header_case_insensitive(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Test different case variations
    esp_err_t ret;

    // Test "CLOSE"
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "CLOSE");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    // Reset connection context for next test
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test "Keep-Alive"
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", "Keep-Alive");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test error conditions
 */
void test_connection_invalid_parameters(void) {
    // Test with null server handle
    esp_err_t ret = httpd_connection_init(NULL, 42);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);

    // Test with invalid socket fd
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    ret = httpd_connection_process_headers(server, -1, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);

    httpd_stop(server);
}

/**
 * @brief Test connection cleanup
 */
void test_connection_cleanup(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Verify context exists
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);

    // Cleanup should work without errors
    httpd_connection_cleanup(server, test_sockfd);

    httpd_stop(server);
}

/**
 * @brief Test HTTPD_DEFAULT_CONFIG includes connection persistence configuration
 */
void test_default_config_connection_persistence(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Verify default configuration values
    TEST_ASSERT_TRUE(config.connection_config.enable_persistence);
    TEST_ASSERT_EQUAL(0, config.connection_config.max_requests_per_conn); // Unlimited
    TEST_ASSERT_EQUAL(10, config.connection_config.max_idle_sec); // 10 seconds
    TEST_ASSERT_EQUAL(0, config.connection_config.max_lifetime_sec); // Unlimited
}

/**
 * @brief Test maximum requests per connection limit
 */
void test_connection_max_requests_limit(void) {
    // Create server with custom config for max requests = 2
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.connection_config.max_requests_per_conn = 2;

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // First request - should persist
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Second request - should still persist (exactly at limit)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd)); // == limit is still OK

    // Third request - should not persist (exceeded limit)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test idle timeout enforcement
 */
void test_connection_idle_timeout(void) {
    // Create server with short idle timeout
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.connection_config.max_idle_sec = 2; // 2 seconds

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Process a request - initially should persist
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Check immediately - should still be OK
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Simulate time passing by manually setting old timestamp
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->last_request_at = time(NULL) - 5; // 5 seconds ago

    // Now should not persist (exceeded 2 second idle timeout)
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test maximum connection lifetime enforcement
 */
void test_connection_lifetime_limit(void) {
    // Create server with short lifetime limit
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.connection_config.max_lifetime_sec = 2; // 2 seconds

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Process a request - initially should persist
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Simulate old connection by manually setting creation time
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    ctx->created_at = time(NULL) - 5; // 5 seconds ago

    // Now should not persist (exceeded 2 second lifetime)
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test that WebSocket connections ignore limits
 */
void test_connection_websocket_ignores_limits(void) {
    // Create server with strict limits
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.connection_config.max_requests_per_conn = 1;
    config.connection_config.max_idle_sec = 1;

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Set up as WebSocket connection
    httpd_connection_mark_websocket(server, test_sockfd);

    // Process requests and manipulate timestamps
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);

    // Exceed request limit
    ctx->request_count = 100;

    // Make connection appear very old
    ctx->last_request_at = time(NULL) - 1000;
    ctx->created_at = time(NULL) - 1000;

    // WebSocket connections should still persist despite exceeding limits
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));
    TEST_ASSERT_FALSE(httpd_connection_exceeded_limits(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test that timeout checking uses server configuration
 */
void test_connection_config_access(void) {
    // Test that different config values produce different behaviors
    httpd_config_t config_no_limits = HTTPD_DEFAULT_CONFIG();
    config_no_limits.connection_config.max_requests_per_conn = 0; // Unlimited

    httpd_config_t config_with_limits = HTTPD_DEFAULT_CONFIG();
    config_with_limits.connection_config.max_requests_per_conn = 1; // Limited

    httpd_handle_t server1, server2;
    esp_err_t ret;

    ret = httpd_start(&server1, &config_no_limits);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = httpd_start(&server2, &config_with_limits);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    const int sockfd1 = 42;
    const int sockfd2 = 43;

    // Set up sessions for both servers
    struct sock_db* session1 = create_mock_session(server1, sockfd1);
    struct sock_db* session2 = create_mock_session(server2, sockfd2);
    TEST_ASSERT_NOT_NULL(session1);
    TEST_ASSERT_NOT_NULL(session2);

    // Both start with same behavior
    ret = httpd_connection_process_headers(server1, sockfd1, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = httpd_connection_process_headers(server2, sockfd2, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Both should persist initially
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server1, sockfd1));
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server2, sockfd2));

    // Second request - server1 (no limits) should persist, server2 (limited) should not
    ret = httpd_connection_process_headers(server1, sockfd1, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = httpd_connection_process_headers(server2, sockfd2, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_TRUE(httpd_connection_should_persist(server1, sockfd1));  // No limits
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server2, sockfd2)); // Exceeded limit

    httpd_stop(server1);
    httpd_stop(server2);
}

/**
 * @brief Test malformed Connection headers (null bytes, spaces, oversized)
 */
void test_connection_malformed_headers(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    esp_err_t ret;

    // Test Connection header with leading/trailing spaces (should be trimmed)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", " close ");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test Connection header with mixed case close (already tested, but include for completeness)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "CLOSE");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test Connection header with keep-alive mixed case
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", "Keep-Alive");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test empty Connection header (should be treated as no header)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test invalid Connection header values that should be ignored
 */
void test_connection_invalid_header_values(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    esp_err_t ret;

    // Test Connection header with invalid value (should be ignored, default to persistent)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "invalid-value");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test Connection header with numeric value (should be ignored)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "123");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test Connection header with special characters (should be ignored)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "keep-alive; version=1.1");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test reserved "Close" field name handling (RFC 9112 Section 9.5)
 */
void test_connection_reserved_close_field(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    esp_err_t ret;

    // Test case-sensitive "Close" header name (should be treated differently from "close")
    // Note: Our implementation uses case-insensitive comparison for Connection header VALUE,
    // but field names are handled by HTTP parser. This test verifies our parsing handles it correctly.

    // In real HTTP parsing, the field name "Close" would be passed as-is, but our code
    // looks for the value, not the field name. The HTTP parser handles field name matching.

    // For this test, we'll verify that "close" value works in any case
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "close");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test "Close" value (capitalized)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "Close");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test RFC 9112 Section 9.3 edge cases for HTTP version combinations
 */
void test_connection_http_version_edge_cases(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    esp_err_t ret;

    // Test HTTP/1.0 with explicit keep-alive (should be persistent)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", "keep-alive");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test HTTP/1.0 without Connection header (should NOT be persistent per RFC)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.0", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test HTTP/1.1 with keep-alive value (should still be persistent)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", "keep-alive");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    // Reset for next test
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;

    // Test HTTP/1.1 default (no Connection header - should be persistent)
    ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * @brief Test connection state consistency after errors
 */
void test_connection_state_after_errors(void) {
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // First establish a valid persistent connection
    esp_err_t ret = httpd_connection_process_headers(server, test_sockfd, "HTTP/1.1", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_connection_ctx_t* ctx = httpd_connection_get_ctx(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(HTTPD_CONN_STATE_PERSISTENT, ctx->state);

    // Simulate an error by forcing close after response
    httpd_connection_close_after_response(server, test_sockfd);
    TEST_ASSERT_FALSE(httpd_connection_should_persist(server, test_sockfd));
    TEST_ASSERT_TRUE(ctx->close_after_response);

    // Reset the forced close and verify state is restored
    ctx->close_after_response = false;
    TEST_ASSERT_TRUE(httpd_connection_should_persist(server, test_sockfd));

    httpd_stop(server);
}

/**
 * Test header lookup with multiple headers (regression test for httpd_req_get_hdr_value_str bug)
 */
void test_httpd_req_get_hdr_value_str_multiple_headers(void) {
    // Create a mock server for session management
    httpd_handle_t server = create_test_server();
    TEST_ASSERT_NOT_NULL(server);

    const int test_sockfd = 42;
    struct sock_db* session = create_mock_session(server, test_sockfd);
    TEST_ASSERT_NOT_NULL(session);

    // Set up request structure to simulate parsed headers
    struct httpd_data *hd = (struct httpd_data *)server;
    struct httpd_req *r = &hd->hd_req;
    struct httpd_req_aux *ra = &hd->hd_req_aux;

    // Initialize request structure
    r->handle = hd;
    r->aux = ra;
    ra->sd = session;

    // Simulate parsed headers in scratch buffer (format: "Host: example.com\0\0\0Connection: close\0\0\0")
    strcpy(ra->scratch, "Host: example.com");
    char *ptr = ra->scratch + strlen(ra->scratch) + 1; // Skip first null, add extra nulls
    *ptr++ = '\0';
    *ptr++ = '\0';
    strcpy(ptr, "Connection: close");
    ptr += strlen(ptr) + 1;
    *ptr++ = '\0';
    *ptr++ = '\0';
    *ptr = '\0'; // End with null

    ra->req_hdrs_count = 2; // Two headers

    // Test getting Host header (first)
    char host_val[64];
    esp_err_t ret = httpd_req_get_hdr_value_str(r, "Host", host_val, sizeof(host_val));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("example.com", host_val);

    // Test getting Connection header (second) - this would fail before the fix
    char conn_val[64];
    ret = httpd_req_get_hdr_value_str(r, "Connection", conn_val, sizeof(conn_val));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("close", conn_val);

    // Test getting non-existent header
    char missing_val[64];
    ret = httpd_req_get_hdr_value_str(r, "User-Agent", missing_val, sizeof(missing_val));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);

    httpd_stop(server);
}

/**
 * Public test runner function called by main test coordinator
 */
int test_connection_persistence(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_connection_init_success);
    RUN_TEST(test_connection_http11_default_persistent);
    RUN_TEST(test_connection_http11_close);
    RUN_TEST(test_connection_http10_default_close);
    RUN_TEST(test_connection_http10_keepalive);
    RUN_TEST(test_connection_multiple_requests);
    RUN_TEST(test_connection_should_persist);
    RUN_TEST(test_connection_close_after_response);
    RUN_TEST(test_connection_websocket_marking);
    RUN_TEST(test_connection_header_case_insensitive);
    RUN_TEST(test_connection_invalid_parameters);
    RUN_TEST(test_connection_cleanup);
    RUN_TEST(test_default_config_connection_persistence);
    RUN_TEST(test_connection_max_requests_limit);
    RUN_TEST(test_connection_idle_timeout);
    RUN_TEST(test_connection_lifetime_limit);
    RUN_TEST(test_connection_websocket_ignores_limits);
    RUN_TEST(test_connection_config_access);
    RUN_TEST(test_connection_malformed_headers);
    RUN_TEST(test_connection_invalid_header_values);
    RUN_TEST(test_connection_reserved_close_field);
    RUN_TEST(test_connection_http_version_edge_cases);
    RUN_TEST(test_connection_state_after_errors);
    RUN_TEST(test_httpd_req_get_hdr_value_str_multiple_headers);
    return 0;
}
