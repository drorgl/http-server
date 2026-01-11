#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_httpd_priv.h"
#include "http_test_client.h"
#include "generic_event_group.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#endif

#include "test_websocket_masking.h"

#define TEST_TIMEOUT_MS 2000
#define TAG "test_websocket_masking"

#define BIT0 (1 << 0)
#define BIT1 (1 << 1)
#define BIT2 (1 << 2)
#define BIT3 (1 << 3)

static event_group_handle_t ws_event_group;
const int WS_CONNECTED_BIT = BIT0;
const int WS_DISCONNECTED_BIT = BIT1;
const int WS_FRAME_SENT_BIT = BIT2;
const int WS_SEND_FAILED_BIT = BIT3;

/**
 * @brief WebSocket masking test handler - validates client masking enforcement
 */
static esp_err_t ws_masking_enforcement_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        LOGD(TAG, "WebSocket handshake completed");
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    // For WebSocket frames, the server enforces masking automatically
    // If we receive a frame, it means the masking check passed
    // Just echo the message back to confirm proper masking/unmasking
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len && ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        // Echo the received message back
        uint8_t *response_buf = (uint8_t*)malloc(ws_pkt.len + 1);
        if (!response_buf) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);
        response_buf[ws_pkt.len] = '\0';

        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = response_buf,
            .len = ws_pkt.len
        };

        LOGD_BUFFER_HEXDUMP(TAG, response_buf, ws_pkt.len, "Server echoing masked message");
        esp_err_t send_ret = httpd_ws_send_frame(req, &response);
        free(response_buf);

        if (send_ret == ESP_OK) {
            event_group_set_bits(ws_event_group, WS_FRAME_SENT_BIT);
        }

        if (ws_pkt.api_allocated_payload) {
            free(ws_pkt.payload);
        }
        return send_ret != ESP_OK ? send_ret : ESP_OK;
    }

    if (ws_pkt.api_allocated_payload) {
        free(ws_pkt.payload);
    }
    return ESP_OK;
}

/**
 * @brief Setup WebSocket server for masking tests
 */
uint16_t setup_websocket_masking_server(httpd_handle_t *handle, httpd_uri_t *ws_uri)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0; // Use port port for masking tests
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(handle, &config));

    // Zero-initialize struct to prevent uninitialized pointer fields from causing crashes
    memset(ws_uri, 0, sizeof(httpd_uri_t));

    ws_uri->uri = "/ws_masking";
    ws_uri->method = HTTP_GET;
    ws_uri->handler = ws_masking_enforcement_handler;
    ws_uri->user_ctx = NULL;
    ws_uri->is_websocket = true;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(*handle, ws_uri));
    return config.server_port;
}

/**
 * @brief Teardown WebSocket masking test server
 */
void teardown_websocket_masking_server(httpd_handle_t handle)
{
    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

/**
 * @brief Test RFC 6455 masking enforcement - unmasked frames should be rejected with close code 1002
 */
void given_unmasked_client_frame_when_received_then_server_closes_with_1002(void)
{
    LOGD(TAG, "Starting unmasked frame rejection test");
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    uint16_t port = setup_websocket_masking_server(&handle, &ws_uri);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, TEST_TIMEOUT_MS));

    // Perform WebSocket handshake
    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_masking", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    // Verify handshake succeeded and connection is established
    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send unmasked frame using the new helper function
    const char *test_message = "This frame is unmasked and should be rejected";
    ws_test_frame_t unmasked_frame = {
        .type = WS_TYPE_TEXT,
        .fin = true,
        .masked = false, // Explicitly unmasked - this should be rejected
        .mask = {0x00, 0x00, 0x00, 0x00}, // Mask key irrelevant for unmasked
        .payload = (uint8_t *)test_message,
        .payload_len = strlen(test_message)
    };

    LOGD_BUFFER_HEXDUMP(TAG, unmasked_frame.payload, unmasked_frame.payload_len, "Sending unmasked frame");
    http_test_client_err_t err = ws_test_client_send_unmasked_frame(client, &unmasked_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should close connection with RFC 6455 Protocol Error (1002)
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, received_frame.type);

    // Verify close code is 1002 (Protocol Error for unmasked client frame)
    TEST_ASSERT_TRUE(received_frame.payload_len >= 2); // Must have at least close code
    uint16_t close_code = (received_frame.payload[0] << 8) | received_frame.payload[1];
    TEST_ASSERT_EQUAL(1002, close_code); // RFC 6455 Protocol Error

    LOGD(TAG, "Server correctly rejected unmasked frame with close code %d", close_code);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_masking_server(handle);
}

/**
 * @brief Test proper masking flow - masked frames should be accepted and correctly unmasked
 */
void given_properly_masked_client_frame_when_received_then_accepted_and_unmasked(void)
{
    LOGD(TAG, "Starting valid masking flow test");
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    uint16_t port = setup_websocket_masking_server(&handle, &ws_uri);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, TEST_TIMEOUT_MS));

    // Perform WebSocket handshake
    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_masking", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    // Verify connection established
    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send properly masked frame
    const char *test_message = "Hello World";
    ws_test_frame_t masked_frame = {
        .type = WS_TYPE_TEXT,
        .fin = true,
        .masked = true, // Properly masked
        .mask = {0x12, 0x34, 0x56, 0x78}, // Test mask key
        .payload = (uint8_t *)test_message,
        .payload_len = strlen(test_message)
    };

    LOGD(TAG, "Sending properly masked frame: '%s'", test_message);
    http_test_client_err_t err = ws_test_client_send_frame(client, &masked_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Wait for server to echo the message back
    bits = event_group_wait_bits(ws_event_group, WS_FRAME_SENT_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_FRAME_SENT_BIT);

    // Receive the echoed message
    ws_test_frame_t echo_frame;
    err = ws_test_client_recv_frame(client, &echo_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_TEXT, echo_frame.type);
    TEST_ASSERT_EQUAL(strlen(test_message), echo_frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(test_message, echo_frame.payload, echo_frame.payload_len);

    LOGD(TAG, "Received correct echo: '%s'", echo_frame.payload);

    // Cleanup
    ws_test_client_free_frame(&echo_frame);
    http_test_client_disconnect(client);
    teardown_websocket_masking_server(handle);
}

/**
 * @brief Test WebSocket masking XOR algorithm with known inputs and expected outputs
 */
void given_known_payload_and_mask_when_xored_then_correctly_transformed(void)
{
    // Test the XOR masking algorithm directly using RFC 6455 specification
    const char *original_payload = "ABCD"; // 65, 66, 67, 68 (ASCII)
    const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    const char *expected_masked = "\x53\x76\x15\x3c"; // Expected XOR result

    size_t payload_len = strlen(original_payload);
    uint8_t *masked_payload = (uint8_t*)malloc(payload_len);

    // Apply masking algorithm manually (same as in http_test_client.c)
    for (size_t i = 0; i < payload_len; i++) {
        masked_payload[i] = ((uint8_t*)original_payload)[i] ^ mask[i % 4];
    }

    // Verify XOR result matches expected
    TEST_ASSERT_EQUAL_MEMORY(expected_masked, masked_payload, payload_len);

    // Test unmasking - applying XOR again should restore original
    uint8_t *unmasked_payload = (uint8_t*)malloc(payload_len);
    for (size_t i = 0; i < payload_len; i++) {
        unmasked_payload[i] = masked_payload[i] ^ mask[i % 4];
    }

    TEST_ASSERT_EQUAL_MEMORY(original_payload, unmasked_payload, payload_len);

    LOGD(TAG, "XOR algorithm test passed");

    // Cleanup
    free(masked_payload);
    free(unmasked_payload);
}

int test_websocket_masking(void) {
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_unmasked_client_frame_when_received_then_server_closes_with_1002);
    RUN_TEST(given_properly_masked_client_frame_when_received_then_accepted_and_unmasked);
    RUN_TEST(given_known_payload_and_mask_when_xored_then_correctly_transformed);
    return 0;
}
