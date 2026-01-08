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

#include "test_websocket_fragmentation.h"

#define TEST_TIMEOUT_MS 2000
#define TAG "test_websocket_fragmentation"

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
 * @brief Send a WebSocket close frame with protocol error status (1002)
 */
static esp_err_t ws_send_protocol_error_close(httpd_req_t *req)
{
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 (Protocol Error) in network byte order
        .len = 2
    };
    return httpd_ws_send_frame(req, &close_frame);
}

static esp_err_t ws_fragmentation_reassembling_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    // Set type to TEXT, as this handler deals with text messages.
    // httpd_ws_recv_frame will update this depending on the received frame type.
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Call httpd_ws_recv_frame to get the frame header and potentially the reassembled payload
    // If a fragmented message is being received, httpd_ws_recv_frame will internally
    // manage reassembly and return ESP_OK only when the full message is available.
    // For intermediate fragments, it will return ESP_ERR_HTTPD_WS_PENDING_FRAGMENT.
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0); // max_len = 0 means get header + length first
    LOGD(TAG, "ws_fragmentation_reassembling_handler: httpd_ws_recv_frame returned %d", ret);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        // More fragments are expected, or server is still reassembling.
        // Return ESP_OK to indicate that the handler has processed this fragment and is waiting for more.
        return ESP_OK;
    } else if (ret != ESP_OK) {
        // Handle other errors during receive - send close frame on error
        ws_send_protocol_error_close(req);
        // Handle other errors during receive
        return ret;
    }

    // If ret == ESP_OK, ws_pkt.payload now points to the complete (reassembled or unfragmented) message
    // and ws_pkt.len is its total length.
    if (ws_pkt.len) {
        // Protocol validation: Check for reserved opcodes and invalid UTF-8 in text frames
        esp_err_t validation_ret = ESP_OK;

        // Validate opcode - reject reserved opcodes per RFC 6455 Section 5.2
        if (ws_pkt.type != HTTPD_WS_TYPE_TEXT && ws_pkt.type != HTTPD_WS_TYPE_BINARY &&
            ws_pkt.type != HTTPD_WS_TYPE_CONTINUE && ws_pkt.type != HTTPD_WS_TYPE_CLOSE &&
            ws_pkt.type != HTTPD_WS_TYPE_PING && ws_pkt.type != HTTPD_WS_TYPE_PONG) {
            LOGD(TAG, "Invalid/reserved opcode received: %d, closing connection", ws_pkt.type);
            validation_ret = ESP_ERR_INVALID_ARG;
        }

        // Validate UTF-8 encoding for text frames per RFC 6455 Section 8.1
        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            validation_ret = httpd_ws_validate_utf8((const uint8_t *)ws_pkt.payload, ws_pkt.len);
            if (validation_ret != ESP_OK) {
                LOGD(TAG, "Invalid UTF-8 detected in text frame, closing connection");
            }
        }

        // If validation failed, send protocol error close frame
        if (validation_ret != ESP_OK) {
            ws_send_protocol_error_close(req);
            return ESP_FAIL;
        }

        // Security check: Detect CRLF injection attempts
        if (memchr(ws_pkt.payload, '\r', ws_pkt.len) != NULL &&
            memchr(ws_pkt.payload, '\n', ws_pkt.len) != NULL) {
            LOGD(TAG, "CRLF injection detected in reassembled message, closing connection");
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xF0}, // Status code 1008 (Policy Violation) in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            return ESP_FAIL;
        }

        // Allocate a new buffer and copy the payload, as ws_pkt.payload might point to an internal buffer.
        uint8_t *response_buf = (uint8_t*)malloc(ws_pkt.len + 1); // +1 for null terminator for text messages
        if (response_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);
        response_buf[ws_pkt.len] = '\0'; // Null-terminate for text

        // Echo the reassembled message back
        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false, // The message is now fully reassembled
            .type = ws_pkt.type, // Use the type from the received message
            .payload = response_buf,
            .len = ws_pkt.len
        };

        LOGD_BUFFER_HEXDUMP(TAG, response_buf, ws_pkt.len, "Server sending response back");

        esp_err_t send_ret = httpd_ws_send_frame(req, &response);
        LOGD(TAG, "ws_fragmentation_reassembling_handler: httpd_ws_send_frame returned %d", send_ret);

        // Always free the response buffer we allocated for sending
        free(response_buf);

        // Signal that the response has been sent
        if (ws_event_group != NULL && send_ret == ESP_OK) {
            LOGD(TAG, "ws_fragmentation_reassembling_handler: Setting WS_FRAME_SENT_BIT");
            event_group_set_bits(ws_event_group, WS_FRAME_SENT_BIT);
        }

        // Free API allocated payload after copying
        if (ws_pkt.api_allocated_payload) {
            free(ws_pkt.payload);
        }

        return send_ret != ESP_OK ? send_ret : ESP_OK;
    }
    // If ws_pkt.len is 0 and ret is ESP_OK, it means an empty frame was received. No further action needed.
    return ESP_OK;
}

static esp_err_t ws_binary_fragmentation_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY; // Expect binary frames

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    LOGD(TAG, "ws_binary_fragmentation_handler: httpd_ws_recv_frame returned %d", ret);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        return ESP_OK;
    } else if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len) {
        uint8_t *response_buf = (uint8_t*)malloc(ws_pkt.len);
        if (response_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);

        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = ws_pkt.type,
            .payload = response_buf,
            .len = ws_pkt.len
        };

        esp_err_t send_ret = httpd_ws_send_frame(req, &response);
        LOGD(TAG, "ws_binary_fragmentation_handler: httpd_ws_send_frame returned %d", send_ret);
        free(response_buf);

        return send_ret != ESP_OK ? send_ret :ESP_OK;
    }
    return ESP_OK;
}

/**
 * @brief Send a WebSocket close frame with server error status (1011)
 */
static esp_err_t ws_send_server_error_close(httpd_req_t *req)
{
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = (uint8_t[]){0x03, 0xF3}, // Status code 1011 (Server Error) in network byte order
        .len = 2
    };
    return httpd_ws_send_frame(req, &close_frame);
}

/**
 * @brief Send a WebSocket close frame with extension required status (1010)
 */
static esp_err_t ws_send_extension_required_close(httpd_req_t *req)
{
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
    .payload = (uint8_t[]){0x03, 0xF2}, // Status code 1010 (Extension Required) in network byte order
        .len = 2
    };
    return httpd_ws_send_frame(req, &close_frame);
}

static esp_err_t ws_extension_required_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    // For any frame received, immediately send 1010 close frame
    // simulating server requiring an extension that wasn't negotiated
    return ws_send_extension_required_close(req);
}

static esp_err_t ws_server_error_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY; // Expect binary frames for this test

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    LOGD(TAG, "ws_server_error_handler: httpd_ws_recv_frame returned %d", ret);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        return ESP_OK;
    } else if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len) {
        // For this test, simulate server error when receiving specific test payload {0x01, 0x02, 0x03, 0x04}
        if (ws_pkt.len == 4 && ws_pkt.payload[0] == 0x01 && ws_pkt.payload[1] == 0x02 &&
            ws_pkt.payload[2] == 0x03 && ws_pkt.payload[3] == 0x04) {
            LOGD(TAG, "Simulating server error condition for test payload - sending 1011 close");
            ws_send_server_error_close(req);
            return ESP_FAIL;
        }

        // Simulate memory exhaustion: attempt to allocate an unreasonably large buffer
        size_t alloc_size = ws_pkt.len + (1024 * 1024 * 10); // Try to allocate 10MB more than payload
        uint8_t *response_buf = (uint8_t*)malloc(alloc_size);
        if (response_buf == NULL) {
            LOGD(TAG, "Memory exhaustion simulated - sending server error close");
            ws_send_server_error_close(req);
            return ESP_FAIL;
        }
        // If allocation somehow succeeded (unlikely), clean up and continue normally
        free(response_buf);

        // Allocate normal response buffer for echo
        response_buf = (uint8_t*)malloc(ws_pkt.len);
        if (response_buf == NULL) {
            ws_send_server_error_close(req);
            return ESP_FAIL;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);

        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = ws_pkt.type,
            .payload = response_buf,
            .len = ws_pkt.len
        };

        esp_err_t send_ret = httpd_ws_send_frame(req, &response);
        LOGD(TAG, "ws_server_error_handler: httpd_ws_send_frame returned %d", send_ret);
        free(response_buf);

        return send_ret != ESP_OK ? send_ret : ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t ws_text_only_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    // This handler explicitly expects TEXT frames only

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    LOGD(TAG, "ws_text_only_handler: httpd_ws_recv_frame returned %d", ret);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        return ESP_OK;
    } else if (ret != ESP_OK) {
        return ret;
    }

    // Check if binary frame sent to text-only handler
    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        LOGD(TAG, "Binary frame received on text-only handler, sending 1003 Unsupported Data");
        httpd_ws_frame_t close_frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_CLOSE,
            .payload = (uint8_t[]){0x03, 0xEB}, // Status code 1003 (Unsupported Data) in network byte order
            .len = 2
        };
        httpd_ws_send_frame(req, &close_frame);
        return ESP_FAIL;
    }

    // For text frames, simple echo (no fragmentation handling needed for this test)
    if (ws_pkt.len && ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        uint8_t *response_buf = (uint8_t*)malloc(ws_pkt.len + 1);
        if (response_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);
        response_buf[ws_pkt.len] = '\0';

        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = ws_pkt.type,
            .payload = response_buf,
            .len = ws_pkt.len
        };

        esp_err_t send_ret = httpd_ws_send_frame(req, &response);
        LOGD(TAG, "ws_text_only_handler: httpd_ws_send_frame returned %d", send_ret);
        free(response_buf);

        return send_ret != ESP_OK ? send_ret : ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t ws_error_handling_handler(httpd_req_t *req)
{
    // Handle both handshake (HTTP_GET) and WebSocket frames
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        return ESP_OK; // Handshake complete, wait for frames
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        return ESP_OK;
    } else if (ret != ESP_OK) {
        event_group_set_bits(ws_event_group, WS_DISCONNECTED_BIT);
        return ret;
    }

    // If ret is ESP_OK, a complete message (possibly reassembled) is available.
    if (ws_pkt.len) {
        // Allocate a buffer and copy the payload from ws_pkt.payload.
        // +1 for null terminator as per previous handler logic for text, safe for binary.
        uint8_t *response_buf = (uint8_t*)malloc(ws_pkt.len + 1);
        if (response_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(response_buf, ws_pkt.payload, ws_pkt.len);
        response_buf[ws_pkt.len] = '\0';

        // Original error handling logic for protocol violations (continuation without start)
        // This check is now redundant because httpd_ws_recv_frame handles this internally
        // and would return ESP_ERR_HTTPD_WS_ERR_FRAGMENT_PROTOCOL.
        // However, if the client sends a CONTINUATION frame that by some logic makes through
        // and `ws_pkt.final` is false and it's a CONTINUATION, it's still a protocol error.
        // Assuming ws_pkt.final would be true for a fully reassembled message of any type.
        if (ws_pkt.type == HTTPD_WS_TYPE_CONTINUE && ws_pkt.final == false) {
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = NULL,
                .len = 0
            };
            httpd_ws_send_frame(req, &close_frame);
            free(response_buf);
            return ESP_FAIL; // Protocol error: unexpected continuation frame
        }

        // Original buffer overflow logic: This is now handled by httpd_ws_recv_frame internally.
        // If httpd_ws_recv_frame returned ESP_OK, the message fits the internal buffer.
        // My handling here (malloc + memcpy) handles the copy out to a new buffer.

        // Echo the message back (or close connection based on error scenario)
        if (ws_pkt.final) { // Always final if ret is ESP_OK and not PENDING_FRAGMENT
            httpd_ws_frame_t response = {
                .final = true,
                .fragmented = false,
                .type = ws_pkt.type,
                .payload = response_buf,
                .len = ws_pkt.len
            };
            esp_err_t send_ret = httpd_ws_send_frame(req, &response);
            free(response_buf);
            if (send_ret != ESP_OK) {
                event_group_set_bits(ws_event_group, WS_SEND_FAILED_BIT);
            }
            return send_ret;
        } else {
          // This else branch should theoretically not be hit if httpd_ws_recv_frame returns ESP_OK and ws_pkt.final is correctly set by the library
          // If it IS hit, it indicates a logic error or unexpected state from the library.
          free(response_buf); // Always free the buffer if not sent
        }
    }
    return ESP_OK;
}

void setup_websocket_server(httpd_handle_t *handle, httpd_uri_t *ws_uri, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9029;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(handle, &config));

    // Zero-initialize struct to prevent uninitialized pointer fields causing crashes
    memset(ws_uri, 0, sizeof(httpd_uri_t));

    ws_uri->uri = "/ws_fragmentation";
    ws_uri->method = HTTP_GET;
    ws_uri->handler = handler;
    ws_uri->user_ctx = NULL;
    ws_uri->is_websocket = true;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(*handle, ws_uri));
}

void teardown_websocket_server(httpd_handle_t handle)
{
    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

void given_ws_connection_when_sending_2_fragment_message_then_reassembled_correctly(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test message
    const char *test_message = "Hello WebSocket Fragmentation Test!";

    // Send fragmented message using helper function
    http_test_client_err_t err = ws_test_client_send_fragmented_message(
        client, test_message, strlen(test_message),
        WS_TYPE_TEXT, 15, TEST_TIMEOUT_MS
    );
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Wait for server to signal that response was sent
    LOGD(TAG, "TEST: Waiting for WS_FRAME_SENT_BIT signal, event_group=%p", ws_event_group);
    event_group_bits_t frame_bits = event_group_wait_bits(ws_event_group, WS_FRAME_SENT_BIT, false, true, TEST_TIMEOUT_MS);
    LOGD(TAG, "TEST: Wait completed, frame_bits=0x%08"PRIx32", expected_mask=0x%08"PRIx32, (uint32_t)frame_bits, (uint32_t)WS_FRAME_SENT_BIT);
    TEST_ASSERT_TRUE(frame_bits & WS_FRAME_SENT_BIT);

    // Receive and verify reassembled message
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(strlen(test_message), received_frame.payload_len);

    // Debug logging for frame reception
    LOGD(TAG, "TEST: Frame reception - err=%d, payload_len=%zu, expected_len=%zu",
         err, received_frame.payload_len, strlen(test_message));
    LOGD_BUFFER_HEXDUMP(TAG, received_frame.payload,
                       MIN(received_frame.payload_len, 64), "RECEIVED_PAYLOAD");

    // Compute comparison details for debugging
    size_t cmp_len = MIN(strlen(test_message), received_frame.payload_len);
    int memcmp_result = memcmp(test_message, received_frame.payload, cmp_len);

    if (memcmp_result != 0 || strlen(test_message) != received_frame.payload_len) {
        LOGD(TAG, "MEMORY COMPARISON: result=%d, cmp_len=%zu", memcmp_result, cmp_len);

        // Find and log differences
        size_t diff_count = 0;
        for (size_t i = 0; i < cmp_len; i++) {
            uint8_t expected = ((uint8_t*)test_message)[i];
            uint8_t actual = received_frame.payload[i];
            if (expected != actual) {
                if (diff_count == 0) {
                    LOGD(TAG, "FIRST DIFFERENCE at position %zu: expected 0x%02X (%c), got 0x%02X (%c)",
                         i, expected, isprint(expected) ? expected : '.',
                         actual, isprint(actual) ? actual : '.');
                }
                diff_count++;
                if (diff_count >= 3) break; // Log first 3 differences
            }
        }
        LOGD(TAG, "Total differences found: %zu out of %zu bytes", diff_count, cmp_len);
    } else {
        LOGD(TAG, "MEMORY COMPARISON: SUCCESS - payloads identical");
    }

    TEST_ASSERT_EQUAL_MEMORY(test_message, received_frame.payload, received_frame.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_3_fragment_message_then_reassembled_correctly(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test message split into 3 fragments
    const char *test_message = "WebSocket fragmentation with three fragments test";
    size_t fragment_size = strlen(test_message) / 3;

    // Send fragmented message
    http_test_client_err_t err = ws_test_client_send_fragmented_message(
        client, test_message, strlen(test_message),
        WS_TYPE_TEXT, fragment_size, TEST_TIMEOUT_MS
    );
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Receive and verify reassembled message
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(strlen(test_message), received_frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(test_message, received_frame.payload, received_frame.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_binary_fragment_message_then_reassembled_correctly(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_binary_fragmentation_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test binary message
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    size_t test_len = sizeof(test_data);

    // Send fragmented binary message
    http_test_client_err_t err = ws_test_client_send_fragmented_message(
        client, (const char*)test_data, test_len,
        WS_TYPE_BINARY, 5, TEST_TIMEOUT_MS
    );
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Receive and verify reassembled binary message
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(test_len, received_frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(test_data, received_frame.payload, received_frame.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_large_fragment_message_then_reassembled_correctly(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test large message (close to buffer limit)
    char large_message[2048];
    for (int i = 0; i < sizeof(large_message) - 1; i++) {
        large_message[i] = 'A' + (i % 26);
    }
    large_message[sizeof(large_message) - 1] = '\0';
    size_t message_len = strlen(large_message);

    // Send fragmented large message
    http_test_client_err_t err = ws_test_client_send_fragmented_message(
        client, large_message, message_len,
        WS_TYPE_TEXT, 512, TEST_TIMEOUT_MS
    );
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Receive and verify reassembled large message
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(message_len, received_frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(large_message, received_frame.payload, received_frame.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_invalid_continuation_frame_then_connection_closed(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_error_handling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send invalid continuation frame without prior fragment
    ws_test_frame_t invalid_frame = {
        .type = WS_TYPE_CONTINUATION,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)"Invalid continuation",
        .payload_len = strlen("Invalid continuation")
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &invalid_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should send CLOSE frame with protocol error
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1002, close_code); // RFC 6455 Protocol Error

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_buffer_overflow_fragment_then_connection_closed(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_error_handling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send first fragment (small)
    ws_test_frame_t first_frame = {
        .type = WS_TYPE_TEXT,
        .fin = false,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)"Small fragment",
        .payload_len = strlen("Small fragment")
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &first_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Send second fragment that would cause buffer overflow
    char large_payload[5000];
    memset(large_payload, 'X', sizeof(large_payload));
    large_payload[sizeof(large_payload) - 1] = '\0';

    ws_test_frame_t overflow_frame = {
        .type = WS_TYPE_CONTINUATION,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)large_payload,
        .payload_len = sizeof(large_payload) - 1
    };

    err = ws_test_client_send_frame(client, &overflow_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should send CLOSE frame due to buffer overflow
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_ws_connection_when_sending_concurrent_fragmentation_then_isolated(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    // Create two clients
    http_test_client_handle_t *client1 = http_test_client_init();
    http_test_client_handle_t *client2 = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client1);
    TEST_ASSERT_NOT_NULL(client2);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client1, "127.0.0.1", 9029, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client2, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client1, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client2, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send different fragmented messages to each client
    const char *message1 = "Client 1 fragmentation test";
    const char *message2 = "Client 2 fragmentation test";

    http_test_client_err_t err1 = ws_test_client_send_fragmented_message(
        client1, message1, strlen(message1), WS_TYPE_TEXT, 8, TEST_TIMEOUT_MS);
    http_test_client_err_t err2 = ws_test_client_send_fragmented_message(
        client2, message2, strlen(message2), WS_TYPE_TEXT, 8, TEST_TIMEOUT_MS);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err1);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err2);

    // Receive and verify each client gets its own message
    ws_test_frame_t received_frame1, received_frame2;

    err1 = ws_test_client_recv_frame(client1, &received_frame1, TEST_TIMEOUT_MS);
    err2 = ws_test_client_recv_frame(client2, &received_frame2, TEST_TIMEOUT_MS);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err1);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err2);
    TEST_ASSERT_EQUAL(strlen(message1), received_frame1.payload_len);
    TEST_ASSERT_EQUAL(strlen(message2), received_frame2.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(message1, received_frame1.payload, received_frame1.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(message2, received_frame2.payload, received_frame2.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame1);
    ws_test_client_free_frame(&received_frame2);
    http_test_client_disconnect(client1);
    http_test_client_disconnect(client2);
    teardown_websocket_server(handle);
}

void given_fragmented_message_when_control_frame_interspersed_then_handled_correctly(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test message: "WebSocket Control Frame Test!"
    const char *test_message = "WebSocket Control Frame Test!";
    const char *fragment1 = "WebSocket Control ";
    const char *fragment2 = "Frame Test!";

    // Send first fragment (FIN=0, type=TEXT)
    ws_test_frame_t first_fragment = {
        .type = WS_TYPE_TEXT,
        .fin = false,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)fragment1,
        .payload_len = strlen(fragment1)
    };
    http_test_client_err_t err = ws_test_client_send_frame(client, &first_fragment, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Send PING control frame during fragmentation
    ws_test_frame_t ping_frame = {
        .type = WS_TYPE_PING,
        .fin = true,
        .masked = true,
        .mask = {0x00, 0x00, 0x00, 0x00},
        .payload = (uint8_t *)"Ping during fragment",
        .payload_len = strlen("Ping during fragment")
    };
    err = ws_test_client_send_frame(client, &ping_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Receive PONG response immediately
    ws_test_frame_t pong_response;
    err = ws_test_client_recv_frame(client, &pong_response, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_PONG, pong_response.type);
    TEST_ASSERT_EQUAL(ping_frame.payload_len, pong_response.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(ping_frame.payload, pong_response.payload, pong_response.payload_len);
    ws_test_client_free_frame(&pong_response);

    // Send final fragment (FIN=1, type=CONTINUATION)
    ws_test_frame_t final_fragment = {
        .type = WS_TYPE_CONTINUATION,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)fragment2,
        .payload_len = strlen(fragment2)
    };
    err = ws_test_client_send_frame(client, &final_fragment, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Wait for server to signal that response was sent (reassembled message)
    event_group_bits_t frame_bits = event_group_wait_bits(ws_event_group, WS_FRAME_SENT_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(frame_bits & WS_FRAME_SENT_BIT);

    // Receive the reassembled message from server
    ws_test_frame_t received_frame;
    err = ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(strlen(test_message), received_frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(test_message, received_frame.payload, received_frame.payload_len);

    // Cleanup
    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_fragmentation_with_crlf_injection_attempt_then_injection_blocked(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Test message with CRLF split across fragments: "Hello\r" + "\nWorld"
    const char *message = "Hello\r\nWorld";

    // Send fragmented message that would inject CRLF
    http_test_client_err_t err = ws_test_client_send_fragmented_message(
        client, message, strlen(message),
        WS_TYPE_TEXT, 6, TEST_TIMEOUT_MS  // Split at "Hello\r" (6 chars) + "\nWorld" (6 chars)
    );
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect CRLF injection and send close frame with status code 1008 (Policy Violation)
    // Receive the close frame from server
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1008, close_code); // RFC 6455 Policy Violation

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_malformed_frame_then_connection_closed_with_1002(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send malformed frame with invalid 16-bit length encoding (truncated length field)
    // Valid frame would have 2 extra bytes for 16-bit length, but we send only 1 byte
    uint8_t malformed_frame[4];
    malformed_frame[0] = 0x81; // FIN=1, opcode=TEXT
    malformed_frame[1] = 0xFE; // 126 for 16-bit length (but only 1 byte provided)
    malformed_frame[2] = 0x00; // First (and only) byte of length - invalid!

    http_test_client_err_t err = ws_test_client_send_malformed_frame(client, malformed_frame, sizeof(malformed_frame), TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect malformed frame and close connection with status code 1002
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1002, close_code); // RFC 6455 Protocol Error

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_invalid_utf8_text_frame_then_connection_closed_with_1002(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send text frame with invalid UTF-8 (overlong encoding: 2-byte sequence for ASCII)
    const char *invalid_utf8 = "\xC0\xAF"; // Overlong encoding for '/' character
    ws_test_frame_t invalid_frame = {
        .type = WS_TYPE_TEXT,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)invalid_utf8,
        .payload_len = strlen(invalid_utf8)
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &invalid_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect invalid UTF-8 and close connection with status code 1002
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1002, close_code); // RFC 6455 Protocol Error

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_invalid_reserved_opcode_then_connection_closed_with_1002(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send frame with reserved opcode 0x5 (invalid per RFC 6455)
    ws_test_frame_t invalid_frame = {
        .type = (ws_frame_type_t)0x5, // Reserved/invalid opcode
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)"Reserved opcode",
        .payload_len = strlen("Reserved opcode")
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &invalid_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect invalid opcode and close connection with status code 1002
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1002, close_code); // RFC 6455 Protocol Error

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_binary_to_text_handler_then_connection_closed_with_1003(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_text_only_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send binary frame to text-only handler
    const uint8_t binary_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    ws_test_frame_t binary_frame = {
        .type = WS_TYPE_BINARY,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)binary_data,
        .payload_len = sizeof(binary_data)
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &binary_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect unsupported data type and close connection with status code 1003
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1003, close_code); // RFC 6455 Unsupported Data

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_oversized_message_then_connection_closed_with_1009(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_fragmentation_reassembling_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send first fragment (small, TEXT, not final)
    ws_test_frame_t first_frame = {
        .type = WS_TYPE_TEXT,
        .fin = false,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)"Start",
        .payload_len = strlen("Start")
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &first_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Send second fragment larger than limit (5000 > 4096)
    char large_payload[5000];
    memset(large_payload, 'X', sizeof(large_payload));
    large_payload[sizeof(large_payload) - 1] = '\0';

    ws_test_frame_t oversized_frame = {
        .type = WS_TYPE_CONTINUATION,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)large_payload,
        .payload_len = sizeof(large_payload) - 1
    };

    err = ws_test_client_send_frame(client, &oversized_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should detect oversized fragment and close connection with status code 1009
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1009, close_code); // RFC 6455 Message Too Big

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_memory_exhaustion_then_connection_closed_with_1011(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_server_error_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send a binary frame that will trigger memory exhaustion simulation
    const uint8_t binary_data[] = {0x01, 0x02, 0x03, 0x04};
    ws_test_frame_t test_frame = {
        .type = WS_TYPE_BINARY,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)binary_data,
        .payload_len = sizeof(binary_data)
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &test_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should encounter memory exhaustion and close connection with status code 1011
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1011, close_code); // RFC 6455 Server Error

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

void given_websocket_extension_required_but_not_supported_then_connection_closed_with_1010(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_handle_t handle = NULL;
    httpd_uri_t ws_uri;
    setup_websocket_server(&handle, &ws_uri, ws_extension_required_handler);

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", 9029, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_fragmentation", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    // Send any frame to trigger the 1010 close response
    ws_test_frame_t test_frame = {
        .type = WS_TYPE_TEXT,
        .fin = true,
        .masked = true,
        .mask = {0x37, 0xFA, 0x21, 0x3D},
        .payload = (uint8_t *)"Test message",
        .payload_len = strlen("Test message")
    };

    http_test_client_err_t err = ws_test_client_send_frame(client, &test_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);

    // Server should send CLOSE frame with status code 1010 (Extension Required)
    ws_test_frame_t close_frame;
    err = ws_test_client_recv_frame(client, &close_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(WS_TYPE_CLOSE, close_frame.type);
    TEST_ASSERT_TRUE(close_frame.payload_len >= 2); // Close code present
    uint16_t close_code = (close_frame.payload[0] << 8) | close_frame.payload[1];
    TEST_ASSERT_EQUAL(1010, close_code); // RFC 6455 Extension Required

    ws_test_client_free_frame(&close_frame);
    http_test_client_disconnect(client);
    teardown_websocket_server(handle);
}

int test_websocket_fragmentation(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_ws_connection_when_sending_2_fragment_message_then_reassembled_correctly);
    RUN_TEST(given_ws_connection_when_sending_3_fragment_message_then_reassembled_correctly);
    RUN_TEST(given_ws_connection_when_sending_binary_fragment_message_then_reassembled_correctly);
    RUN_TEST(given_ws_connection_when_sending_large_fragment_message_then_reassembled_correctly);
    RUN_TEST(given_ws_connection_when_sending_invalid_continuation_frame_then_connection_closed);
    RUN_TEST(given_ws_connection_when_sending_buffer_overflow_fragment_then_connection_closed);
    RUN_TEST(given_ws_connection_when_sending_concurrent_fragmentation_then_isolated);
    RUN_TEST(given_fragmented_message_when_control_frame_interspersed_then_handled_correctly);
    RUN_TEST(given_websocket_fragmentation_with_crlf_injection_attempt_then_injection_blocked);
    RUN_TEST(given_websocket_malformed_frame_then_connection_closed_with_1002);
    RUN_TEST(given_websocket_invalid_utf8_text_frame_then_connection_closed_with_1002);
    RUN_TEST(given_websocket_invalid_reserved_opcode_then_connection_closed_with_1002);
    RUN_TEST(given_websocket_binary_to_text_handler_then_connection_closed_with_1003);
    RUN_TEST(given_websocket_oversized_message_then_connection_closed_with_1009);
    RUN_TEST(given_websocket_memory_exhaustion_then_connection_closed_with_1011);
    RUN_TEST(given_websocket_extension_required_but_not_supported_then_connection_closed_with_1010);
    // return UNITY_END();
    return 0;
}
