#ifndef HTTP_TEST_CLIENT_H
#define HTTP_TEST_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

// --- Common Definitions ---

typedef enum {
    HTTP_TEST_CLIENT_OK = 0,
    HTTP_TEST_CLIENT_ERR_GENERIC,
    HTTP_TEST_CLIENT_ERR_CONNECT,
    HTTP_TEST_CLIENT_ERR_SEND,
    HTTP_TEST_CLIENT_ERR_RECV,
    HTTP_TEST_CLIENT_ERR_TIMEOUT,
    HTTP_TEST_CLIENT_ERR_INVALID_ARG,
    HTTP_TEST_CLIENT_ERR_BUFFER_TOO_SMALL,
    HTTP_TEST_CLIENT_ERR_PROTOCOL, // For HTTP/WS protocol errors
    HTTP_TEST_CLIENT_ERR_NOT_FOUND, // For specific header/cookie not found
} http_test_client_err_t;

typedef struct {
    int sockfd;
    // Add any other client-specific state here, e.g., connection status
} http_test_client_handle_t;

// --- HTTP Definitions ---

typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    // Add other methods as needed
} http_method_t;

typedef struct {
    int status_code;
    char status_text[64];
    char *headers; // Dynamically allocated string of "Key: Value\r\n" pairs
    char *body;    // Dynamically allocated response body
    size_t body_len;
} http_test_response_t;

// --- WebSocket Definitions ---

typedef enum {
    WS_TYPE_CONTINUATION = 0x0,
    WS_TYPE_TEXT         = 0x1,
    WS_TYPE_BINARY       = 0x2,
    WS_TYPE_CLOSE        = 0x8,
    WS_TYPE_PING         = 0x9,
    WS_TYPE_PONG         = 0xA,
} ws_frame_type_t;

typedef struct {
    ws_frame_type_t type;
    bool fin;
    bool masked;
    uint8_t mask[4];
    uint8_t *payload; // Dynamically allocated frame payload
    size_t payload_len;
} ws_test_frame_t;

// --- Client Initialization and Connection ---

/**
 * @brief Initializes a new HTTP client handle.
 * @return A pointer to the client handle, or NULL on failure.
 */
http_test_client_handle_t* http_test_client_init(void);

/**
 * @brief Connects the client to a specified host and port.
 * @param client_handle The client handle.
 * @param host The target host (IP address or hostname).
 * @param port The target port.
 * @param timeout_ms Connection timeout in milliseconds.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t http_test_client_connect(http_test_client_handle_t *client_handle, const char *host, uint16_t port, uint32_t timeout_ms);

/**
 * @brief Disconnects and cleans up the client handle.
 * @param client_handle The client handle.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t http_test_client_disconnect(http_test_client_handle_t *client_handle);

// --- HTTP Request/Response Functions ---

/**
 * @brief Sends an HTTP request and receives the response.
 * @param client_handle The client handle.
 * @param method The HTTP method (GET, POST, etc.).
 * @param uri The request URI (e.g., "/path?query=val").
 * @param headers_str Optional string of custom headers (e.g., "Header1: Value1\r\nHeader2: Value2").
 * @param body Optional request body for POST/PUT.
 * @param body_len Length of the request body.
 * @param response Pointer to a http_test_response_t structure to fill with response data.
 *                 The caller is responsible for freeing response->headers and response->body.
 * @param timeout_ms Timeout for sending and receiving the full response.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t http_test_client_send_request(http_test_client_handle_t *client_handle,
                                                     http_method_t method,
                                                     const char *uri,
                                                     const char *headers_str,
                                                     const char *body, size_t body_len,
                                                     http_test_response_t *response,
                                                     uint32_t timeout_ms);

/**
 * @brief Frees memory allocated for a http_test_response_t.
 * @param response The response structure.
 */
void http_test_client_free_response(http_test_response_t *response);

/**
 * @brief Retrieves the value of a specific header from an HTTP response.
 * @param response The HTTP response structure.
 * @param header_name The name of the header to retrieve (case-insensitive).
 * @return A pointer to a newly allocated string containing the header value, or NULL if the header is not found or memory allocation fails.
 *         The returned string must be freed by the caller.
 */
const char* http_test_client_get_header(const http_test_response_t *response, const char *header_name);

// --- WebSocket Functions ---

/**
 * @brief Performs a WebSocket upgrade handshake.
 * @param client_handle The client handle.
 * @param uri The WebSocket URI (e.g., "/ws").
 * @param host The host for the Host header.
 * @param client_key The Sec-WebSocket-Key to send.
 * @param expected_accept_key The expected Sec-WebSocket-Accept key from the server.
 * @param timeout_ms Timeout for the handshake.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t ws_test_client_handshake(http_test_client_handle_t *client_handle,
                                                const char *uri,
                                                const char *host,
                                                const char *client_key,
                                                const char *expected_accept_key,
                                                uint32_t timeout_ms);

/**
 * @brief Sends a WebSocket data frame.
 * @param client_handle The client handle.
 * @param frame The WebSocket frame to send. The payload should be unmasked.
 * @param timeout_ms Timeout for sending.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t ws_test_client_send_frame(http_test_client_handle_t *client_handle,
                                                 const ws_test_frame_t *frame,
                                                 uint32_t timeout_ms);

/**
 * @brief Receives a WebSocket data frame.
 * @param client_handle The client handle.
 * @param frame Pointer to a ws_test_frame_t structure to fill with received data.
 *              The caller is responsible for freeing frame->payload.
 * @param timeout_ms Timeout for receiving.
 * @return HTTP_TEST_CLIENT_OK on success, or an error code.
 */
http_test_client_err_t ws_test_client_recv_frame(http_test_client_handle_t *client_handle,
                                                 ws_test_frame_t *frame,
                                                 uint32_t timeout_ms);

/**
 * @brief Frees memory allocated for a ws_test_frame_t.
 * @param frame The frame structure.
 */
void ws_test_client_free_frame(ws_test_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif // HTTP_TEST_CLIENT_H
