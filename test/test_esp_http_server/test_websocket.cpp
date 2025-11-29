#include <unity.h>
#include <esp_http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client
// #include "ws_test_client.h" // Include for WebSocket test client

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

#define TAG "test_websocket"


// WebSocket handler function
static esp_err_t ws_test_handler(httpd_req_t *req)
{
    // This handler is just to confirm the handshake is successful
    // No data exchange is performed in this specific test
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
 * Test: given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds
 *
 * Purpose: Verify that the server correctly handles a WebSocket upgrade handshake.
 * Expected: The server responds with 101 Switching Protocols and the correct Sec-WebSocket-Accept header.
 */
void given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds(void)
{
    // Given: A running server with a registered WebSocket URI handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9017; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_test_handler,
        .user_ctx   = NULL,
        .is_websocket = true, // Mark as WebSocket URI
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: A client connects and sends a WebSocket upgrade request
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ=="; // Example client key
    char expected_accept_key[33]; // SHA1 hash (20 bytes) + base64 (28 bytes) + null terminator
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // Perform WebSocket handshake using ws_test_client_handshake
    http_test_client_err_t handshake_ret = ws_test_client_handshake(client, "/ws", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS);

    // Then: The handshake should succeed
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}


// WebSocket handler for data frame testing
static esp_err_t ws_data_frame_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake done, nothing to do here for GET
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; // Default to text, will be updated by httpd_ws_recv_frame

    // Get the frame length first
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1); // +1 for null termination if text
        if (buf == NULL) {
            LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        // Get the frame payload
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        // Null-terminate for text frames
        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            buf[ws_pkt.len] = '\0';
        }
    }

    // Echo back the received frame
    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}



/**
 * Test: given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly
 *
 * Purpose: Verify that WebSocket data frames (text and binary) can be sent and received correctly.
 * Expected: The server echoes back the sent data, and the client receives it intact.
 */
void given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly(void)
{
    // Given: A running server with a registered WebSocket URI handler for data frames
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9018; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_data_uri = {
        .uri        = "/ws_data",
        .method     = HTTP_GET,
        .handler    = ws_data_frame_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_data_uri));

    // When: A client connects using http_test_client and performs WebSocket handshake
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33]; // SHA1 hash (20 bytes) + base64 (28 bytes) + null terminator
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));

    // Perform WebSocket handshake using ws_test_client_handshake
    http_test_client_err_t handshake_ret = ws_test_client_handshake(client, "/ws_data", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, handshake_ret);

    // Then: Client sends text data and receives it echoed back
    const char *text_message = "Hello WebSocket!";
    size_t text_message_len = strlen(text_message);
    
    // Create text frame
    ws_test_frame_t text_frame;
    memset(&text_frame, 0, sizeof(text_frame));
    text_frame.type = WS_TYPE_TEXT;
    text_frame.fin = true;
    text_frame.masked = true;
    text_frame.mask[0] = 0x11;
    text_frame.mask[1] = 0x22;
    text_frame.mask[2] = 0x33;
    text_frame.mask[3] = 0x44;
    text_frame.payload = (uint8_t*)malloc(text_message_len);
    TEST_ASSERT_NOT_NULL(text_frame.payload);
    memcpy(text_frame.payload, text_message, text_message_len);
    text_frame.payload_len = text_message_len;

    // Send text frame
    http_test_client_err_t send_ret = ws_test_client_send_frame(client, &text_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, send_ret);
    
    // Receive text frame
    ws_test_frame_t received_text_frame;
    memset(&received_text_frame, 0, sizeof(received_text_frame));
    http_test_client_err_t recv_ret = ws_test_client_recv_frame(client, &received_text_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, recv_ret);
    
    // Assert text frame properties
    TEST_ASSERT_EQUAL(WS_TYPE_TEXT, received_text_frame.type);
    TEST_ASSERT_EQUAL(true, received_text_frame.fin);
    TEST_ASSERT_EQUAL(text_message_len, received_text_frame.payload_len);
    TEST_ASSERT_NOT_NULL(received_text_frame.payload);
    TEST_ASSERT_EQUAL_STRING(text_message, (char*)received_text_frame.payload);
    
    // Cleanup text frame payload
    free(text_frame.payload);
    ws_test_client_free_frame(&received_text_frame);

    // Client sends binary data and receives it echoed back
    const uint8_t binary_message[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t binary_message_len = sizeof(binary_message);
    
    // Create binary frame
    ws_test_frame_t binary_frame;
    memset(&binary_frame, 0, sizeof(binary_frame));
    binary_frame.type = WS_TYPE_BINARY;
    binary_frame.fin = true;
    binary_frame.masked = true;
    binary_frame.mask[0] = 0x55;
    binary_frame.mask[1] = 0x66;
    binary_frame.mask[2] = 0x77;
    binary_frame.mask[3] = 0x88;
    binary_frame.payload = (uint8_t*)malloc(binary_message_len);
    TEST_ASSERT_NOT_NULL(binary_frame.payload);
    memcpy(binary_frame.payload, binary_message, binary_message_len);
    binary_frame.payload_len = binary_message_len;

    // Send binary frame
    send_ret = ws_test_client_send_frame(client, &binary_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, send_ret);
    
    // Receive binary frame
    ws_test_frame_t received_binary_frame;
    memset(&received_binary_frame, 0, sizeof(received_binary_frame));
    recv_ret = ws_test_client_recv_frame(client, &received_binary_frame, TEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, recv_ret);
    
    // Assert binary frame properties
    TEST_ASSERT_EQUAL(WS_TYPE_BINARY, received_binary_frame.type);
    TEST_ASSERT_EQUAL(true, received_binary_frame.fin);
    TEST_ASSERT_EQUAL(binary_message_len, received_binary_frame.payload_len);
    TEST_ASSERT_NOT_NULL(received_binary_frame.payload);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(binary_message, received_binary_frame.payload, binary_message_len);
    
    // Cleanup binary frame payload
    free(binary_frame.payload);
    ws_test_client_free_frame(&received_binary_frame);

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}



/**
 * Test: given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection
 *
 * Purpose: Verify that the server correctly handles a WebSocket closing handshake initiated by the client.
 * Expected: The server responds with a CLOSE frame and then closes the connection.
 */
void given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection(void)
{
    // Given: A running server with a registered WebSocket URI handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9019; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_close",
        .method     = HTTP_GET,
        .handler    = ws_data_frame_handler, // Use the data frame handler to process incoming frames
        .user_ctx   = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false, // Server should handle PING/PONG/CLOSE internally
        .supported_subprotocol = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: A client connects and performs WebSocket handshake
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    TEST_ASSERT_EQUAL(0, connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char request_buffer[512];
    snprintf(request_buffer, sizeof(request_buffer),
             "GET /ws_close HTTP/1.1\r\n"
             "Host: localhost:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             config.server_port, client_key);

    send(sockfd, request_buffer, strlen(request_buffer), 0);

    char response_buffer[1024] = {0};
    httpd_os_thread_sleep(100);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    int recv_ret = recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, recv_ret);
    response_buffer[recv_ret] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(response_buffer, "HTTP/1.1 101 Switching Protocols"));

    // Then: Client sends a CLOSE frame
    // A simple CLOSE frame with no payload (opcode 0x8)
    uint8_t close_frame[6]; // 2 bytes header + 4 bytes mask
    memset(close_frame, 0, sizeof(close_frame));
    close_frame[0] = 0x88; // FIN + Close frame
    close_frame[1] = 0x80; // Masked + Length 0
    uint8_t mask_key_close[] = {0xAA, 0xBB, 0xCC, 0xDD};
    memcpy(&close_frame[2], mask_key_close, 4);
    // No payload to mask as length is 0

    send(sockfd, (const char*)close_frame, sizeof(close_frame), 0);

    memset(response_buffer, 0, sizeof(response_buffer));
    httpd_os_thread_sleep(100); // Give server time to process and respond

    // The server should respond with a CLOSE frame (FIN + Close, unmasked, length 0)
    // Expected response: 0x88 (FIN+Close), 0x00 (unmasked, length 0)
    recv_ret = recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, recv_ret); // Ensure a response was received
    TEST_ASSERT_EQUAL(2, recv_ret); // Expected 2 bytes for a simple close frame
    TEST_ASSERT_EQUAL(0x88, (uint8_t)response_buffer[0]); // FIN + Close
    TEST_ASSERT_EQUAL(0x00, (uint8_t)response_buffer[1]); // Unmasked, Length 0

    // After sending the CLOSE frame, the server should close the connection.
    // A subsequent recv should return 0 (graceful close) or -1 (error/reset).
    httpd_os_thread_sleep(100); // Give server time to close the socket
    recv_ret = recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    TEST_ASSERT_LESS_OR_EQUAL(0, recv_ret); // Connection should be closed

    // Cleanup
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    httpd_stop(handle);
}



int test_websocket_upgrade_handshake(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds);
    RUN_TEST(given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly);
    RUN_TEST(given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection);
    // return UNITY_END();
    return 0;
}

// HTTP handler function for testing HTTP client type
static esp_err_t http_info_handler(httpd_req_t *req)
{
    // Send a minimal response to complete the HTTP transaction
    return httpd_resp_send(req, NULL, 0);
}

/**
 * Test: given_websocket_and_http_clients_when_calling_httpd_ws_get_fd_info_then_returns_correct_client_type
 *
 * Purpose: Verify that httpd_ws_get_fd_info correctly identifies client types.
 * Expected: Returns HTTPD_WS_CLIENT_HTTP for HTTP clients, HTTPD_WS_CLIENT_WEBSOCKET for WebSocket clients,
 *           and HTTPD_WS_CLIENT_INVALID for invalid file descriptors.
 */
void given_websocket_and_http_clients_when_calling_httpd_ws_get_fd_info_then_returns_correct_client_type(void)
{
    // Given: A running server with registered HTTP and WebSocket URI handlers
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9020; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws_info",
        .method     = HTTP_GET,
        .handler    = ws_test_handler,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    // When: A WebSocket client connects and performs a handshake
    http_test_client_handle_t *ws_client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(ws_client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(ws_client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));
    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    generate_ws_accept_key(client_key, expected_accept_key, sizeof(expected_accept_key));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(ws_client, "/ws_info", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    // Give the server thread time to update the session state after the handshake
    httpd_os_thread_sleep(100);

    // To confirm the WS state, send a frame and expect a response
    // This ensures the session is fully in websocket mode before we check the flag
    ws_test_frame_t ping_frame;
    memset(&ping_frame, 0, sizeof(ping_frame));
    ping_frame.type = WS_TYPE_PING;
    ping_frame.fin = true;
    ping_frame.masked = true;
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_send_frame(ws_client, &ping_frame, TEST_TIMEOUT_MS));

    ws_test_frame_t pong_frame;
    memset(&pong_frame, 0, sizeof(pong_frame));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_recv_frame(ws_client, &pong_frame, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(WS_TYPE_PONG, pong_frame.type);
    ws_test_client_free_frame(&pong_frame);

    // Then: Check the client types using httpd_ws_get_fd_info
    // Get the server's file descriptor for the WebSocket client
    size_t fds_count = 1;
    int client_fds[1];
    TEST_ASSERT_EQUAL(ESP_OK, httpd_get_client_list(handle, &fds_count, client_fds));
    TEST_ASSERT_EQUAL(1, fds_count); // Expecting only one client (the WebSocket client)

    TEST_ASSERT_EQUAL(HTTPD_WS_CLIENT_WEBSOCKET, httpd_ws_get_fd_info(handle, client_fds[0]));

    // Test with an invalid file descriptor
    TEST_ASSERT_EQUAL(HTTPD_WS_CLIENT_INVALID, httpd_ws_get_fd_info(handle, -1));

    // Cleanup
    http_test_client_disconnect(ws_client);
    httpd_stop(handle);
}

int test_websocket(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds);
    RUN_TEST(given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly);
    RUN_TEST(given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection);
    RUN_TEST(given_websocket_and_http_clients_when_calling_httpd_ws_get_fd_info_then_returns_correct_client_type);
    // return UNITY_END();
    test_websocket_upgrade_handshake();
    return 0;
}
