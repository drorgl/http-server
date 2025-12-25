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
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client
#include "test_websocket_extensions_e2e.h"

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

#include <sha1.h>
#include <base64_codec.h>

#define TEST_TIMEOUT_MS 1000

#define TAG "test_ws_ext_e2e"

// WebSocket handler function
static esp_err_t ws_e2e_test_handler(httpd_req_t *req)
{
    // Simple handler that just confirms handshake succeeded
    return ESP_OK;
}

// The magic GUID string used for handshake
static const char ws_magic_uuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Helper function to generate Sec-WebSocket-Accept key
static void generate_ws_accept_key(const char *client_key, char *server_key_out, size_t out_buf_len) {
    char server_raw_text[128]; // Sufficiently large buffer
    snprintf(server_raw_text, sizeof(server_raw_text), "%s%s", client_key, ws_magic_uuid);

    uint8_t server_key_hash[20];
    sha1_context_t sha1_ctx;
    sha1_init(&sha1_ctx);
    sha1_update(&sha1_ctx, (const uint8_t *)server_raw_text, strlen(server_raw_text));
    sha1_final(&sha1_ctx, server_key_hash);

    size_t encoded_len = 0;
    base64_encode(server_key_hash, sizeof(server_key_hash),
                          server_key_out, out_buf_len); // Use base64_encode
}

/**
 * Test: test_e2e_websocket_extensions_single_negotiation
 *
 * Purpose: Test E2E WebSocket extension negotiation with single extension (permessage-deflate)
 * Expected: Client offers "permessage-deflate", server supports it, handshake succeeds with negotiated extensions
 */
void test_e2e_websocket_extensions_single_negotiation(void)
{
    TEST_MESSAGE("E2E: Single extension negotiation (permessage-deflate)");

    // Given: Server with WebSocket handler supporting permessage-deflate extension
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9030; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_ext_e2e",
        .method     = HTTP_GET,
        .handler    = ws_e2e_test_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .supported_extensions = "permessage-deflate"
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: Client connects and offers permessage-deflate extension using http-test-client
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ=="; // "the sample nonce"
    char expected_accept_key[33];
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // Offer permessage-deflate extension
    const char *offered_extensions = "permessage-deflate";
    char *negotiated_extensions = NULL;

    // Perform handshake with extensions
    http_test_client_err_t handshake_ret = ws_test_client_handshake_with_extensions(
        client, "/ws_ext_e2e", "127.0.0.1", client_key, expected_accept_key,
        offered_extensions, &negotiated_extensions, TEST_TIMEOUT_MS);

    // Then: Handshake should succeed and extensions should be negotiated
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);
    TEST_ASSERT_NOT_NULL(negotiated_extensions);
    TEST_ASSERT_EQUAL_STRING("permessage-deflate", negotiated_extensions);

    // Cleanup
    free(negotiated_extensions);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_websocket_extensions_multiple_negotiation
 *
 * Purpose: Test E2E WebSocket extension negotiation with multiple extensions (server supports compress, client offers deflate-stream and compress)
 * Expected: Client offers multiple extensions, server negotiates only supported "compress"
 */
void test_e2e_websocket_extensions_multiple_negotiation(void)
{
    TEST_MESSAGE("E2E: Multiple extension negotiation (negotiate compress from deflate-stream, compress offers)");

    // Given: Server with WebSocket handler supporting only "compress" extension
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9031; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_ext_multi",
        .method     = HTTP_GET,
        .handler    = ws_e2e_test_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .supported_extensions = "compress"
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: Client connects and offers multiple extensions including one server supports
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // Offer deflate-stream (unsupported) and compress (supported)
    const char *offered_extensions = "deflate-stream, compress";
    char *negotiated_extensions = NULL;

    // Perform handshake with extensions
    http_test_client_err_t handshake_ret = ws_test_client_handshake_with_extensions(
        client, "/ws_ext_multi", "127.0.0.1", client_key, expected_accept_key,
        offered_extensions, &negotiated_extensions, TEST_TIMEOUT_MS);

    // Then: Handshake should succeed but only "compress" should be negotiated
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);
    TEST_ASSERT_NOT_NULL(negotiated_extensions);
    TEST_ASSERT_EQUAL_STRING("compress", negotiated_extensions);

    // Cleanup
    free(negotiated_extensions);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_websocket_extensions_no_common_extensions
 *
 * Purpose: Test E2E WebSocket extension negotiation when client and server have no common extensions
 * Expected: Handshake succeeds but no Sec-WebSocket-Extensions header is returned
 */
void test_e2e_websocket_extensions_no_common_extensions(void)
{
    TEST_MESSAGE("E2E: No common extensions negotiation (client offers unsupported extensions)");

    // Given: Server with WebSocket handler supporting "permessage-deflate" extension
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9032; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_ext_none",
        .method     = HTTP_GET,
        .handler    = ws_e2e_test_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .supported_extensions = "permessage-deflate"
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: Client connects and offers extensions that server doesn't support
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // Offer extensions server doesn't support
    const char *offered_extensions = "x-custom-ext, another-unsupported";
    char *negotiated_extensions = NULL;

    // Perform handshake with extensions
    http_test_client_err_t handshake_ret = ws_test_client_handshake_with_extensions(
        client, "/ws_ext_none", "127.0.0.1", client_key, expected_accept_key,
        offered_extensions, &negotiated_extensions, TEST_TIMEOUT_MS);

    // Then: Handshake should succeed but negotiated_extensions should be NULL (no common extensions)
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);
    TEST_ASSERT_NULL(negotiated_extensions);

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: test_e2e_websocket_extensions_no_extensions_offered
 *
 * Purpose: Test E2E WebSocket when client offers no extensions (backward compatibility)
 * Expected: Handshake succeeds with no negotiated extensions
 */
void test_e2e_websocket_extensions_no_extensions_offered(void)
{
    TEST_MESSAGE("E2E: No extensions offered (backward compatibility test)");

    // Given: Server with WebSocket handler that supports extensions
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9033; // Unique port for this test
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_ext_back_compat",
        .method     = HTTP_GET,
        .handler    = ws_e2e_test_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .supported_extensions = "permessage-deflate"
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: Client connects without offering any extensions (NULL offered_extensions)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // No extensions offered (NULL)
    char *negotiated_extensions = NULL;

    // Perform handshake without extensions
    http_test_client_err_t handshake_ret = ws_test_client_handshake_with_extensions(
        client, "/ws_ext_back_compat", "127.0.0.1", client_key, expected_accept_key,
        NULL, &negotiated_extensions, TEST_TIMEOUT_MS);

    // Then: Handshake should succeed and negotiated_extensions should be NULL (no extensions negotiated)
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);
    TEST_ASSERT_NULL(negotiated_extensions);

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

int test_websocket_extensions_e2e(void) {
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_e2e_websocket_extensions_single_negotiation);
    RUN_TEST(test_e2e_websocket_extensions_multiple_negotiation);
    RUN_TEST(test_e2e_websocket_extensions_no_common_extensions);
    RUN_TEST(test_e2e_websocket_extensions_no_extensions_offered);

    return 0; // Success
}
