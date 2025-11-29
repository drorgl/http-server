#include "http_test_client.h"
#include <esp_http_server.h>
#include <stdlib.h> // For malloc, free
#include <string.h> // For memset, strlen, strncpy
#include <stdio.h>  // For snprintf
#include <ctype.h>  // For tolower

#include "sha1.h" // For WebSocket key generation
#include "base64_codec.h" // For WebSocket key generation

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // For getaddrinfo
#pragma comment(lib, "ws2_32.lib") // Link with ws2_32.lib
#else
#include <sys/socket.h>
#include <netdb.h> // For getaddrinfo
#include <arpa/inet.h> // For inet_addr
#include <unistd.h> // for close
#include <netinet/in.h> // For in_port_t on Linux
#include <fcntl.h> // For fcntl
#include <errno.h> // For errno
#endif

#include <log.h>

static const char * TAG = "TEST-CLIENT";

// Helper function to set socket timeout
static http_test_client_err_t set_socket_timeout(int sockfd, uint32_t timeout_ms) {
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }
#endif
    return HTTP_TEST_CLIENT_OK;
}

// Helper function to send data with timeout
static http_test_client_err_t send_data(int sockfd, const char *data, size_t len, uint32_t timeout_ms) {
    size_t total_sent = 0;
    int bytes_left = len;
    int ret;

    while (total_sent < len) {
        ret = send(sockfd, data + total_sent, bytes_left, 0);
        if (ret == -1) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAETIMEDOUT) {
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
#endif
                // Timeout or non-blocking socket would block, retry
                // For simplicity, we'll just return timeout error here
                return HTTP_TEST_CLIENT_ERR_TIMEOUT;
            }
            return HTTP_TEST_CLIENT_ERR_SEND;
        }
        total_sent += ret;
        bytes_left -= ret;
    }
    return HTTP_TEST_CLIENT_OK;
}

// Helper function to receive data with timeout
static int recv_data(int sockfd, char *buf, size_t buf_len, uint32_t timeout_ms) {
    int ret = recv(sockfd, buf, buf_len, 0);
    if (ret == -1) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
#endif
            return 0; // Timeout, no data received yet
        }
        return -1; // Error
    }
    return ret;
}

http_test_client_handle_t* http_test_client_init(void) {
    http_test_client_handle_t *handle = (http_test_client_handle_t*) calloc(1, sizeof(http_test_client_handle_t));
    if (handle == NULL) {
        return NULL;
    }
    handle->sockfd = -1; // Initialize to an invalid socket descriptor

// #ifdef _WIN32
//     WSADATA wsaData;
//     int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
//     if (iResult != 0) {
//         free(handle);
//         return NULL;
//     }
// #endif
    return handle;
}

http_test_client_err_t http_test_client_connect(http_test_client_handle_t *client_handle, const char *host, uint16_t port, uint32_t timeout_ms) {
    if (client_handle == NULL || host == NULL || port == 0) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }

    struct addrinfo hints, *res, *p;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return HTTP_TEST_CLIENT_ERR_CONNECT;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        client_handle->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (client_handle->sockfd == -1) {
            continue;
        }

        // Set non-blocking for connect with timeout
#ifdef _WIN32
        u_long mode = 1; // 1 for non-blocking
        ioctlsocket(client_handle->sockfd, FIONBIO, &mode);
#else
        fcntl(client_handle->sockfd, F_SETFL, O_NONBLOCK);
#endif

        int connect_ret = connect(client_handle->sockfd, p->ai_addr, p->ai_addrlen);

        if (connect_ret == -1) {
#ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                closesocket(client_handle->sockfd);
                client_handle->sockfd = -1;
                continue;
            }
#else
            if (errno != EINPROGRESS) {
                close(client_handle->sockfd);
                client_handle->sockfd = -1;
                continue;
            }
#endif
        }

        // Wait for connection with timeout
        struct timeval tv;
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(client_handle->sockfd, &write_fds);

        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int select_ret = select(client_handle->sockfd + 1, NULL, &write_fds, NULL, &tv);

        if (select_ret == -1) {
            // Error in select
#ifdef _WIN32
            closesocket(client_handle->sockfd);
#else
            close(client_handle->sockfd);
#endif
            client_handle->sockfd = -1;
            freeaddrinfo(res);
            return HTTP_TEST_CLIENT_ERR_CONNECT;
        } else if (select_ret == 0) {
            // Timeout
#ifdef _WIN32
            closesocket(client_handle->sockfd);
#else
            close(client_handle->sockfd);
#endif
            client_handle->sockfd = -1;
            freeaddrinfo(res);
            return HTTP_TEST_CLIENT_ERR_TIMEOUT;
        } else {
            // Connection established or error
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(client_handle->sockfd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) < 0 || err != 0) {
                // Connection error
#ifdef _WIN32
                closesocket(client_handle->sockfd);
#else
                close(client_handle->sockfd);
#endif
                client_handle->sockfd = -1;
                continue;
            }
        }

        // Set back to blocking mode and apply read/write timeouts
#ifdef _WIN32
        mode = 0; // 0 for blocking
        ioctlsocket(client_handle->sockfd, FIONBIO, &mode);
#else
        fcntl(client_handle->sockfd, F_SETFL, 0);
#endif
        if (set_socket_timeout(client_handle->sockfd, timeout_ms) != HTTP_TEST_CLIENT_OK) {
#ifdef _WIN32
            closesocket(client_handle->sockfd);
#else
            close(client_handle->sockfd);
#endif
            client_handle->sockfd = -1;
            freeaddrinfo(res);
            return HTTP_TEST_CLIENT_ERR_GENERIC;
        }

        freeaddrinfo(res);
        return HTTP_TEST_CLIENT_OK; // Connection successful
    }

    freeaddrinfo(res);
    return HTTP_TEST_CLIENT_ERR_CONNECT; // No successful connection
}

http_test_client_err_t http_test_client_disconnect(http_test_client_handle_t *client_handle) {
    if (client_handle == NULL) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }

    if (client_handle->sockfd != -1) {
#ifdef _WIN32
        closesocket(client_handle->sockfd);
        // WSACleanup();
#else
        close(client_handle->sockfd);
#endif
        client_handle->sockfd = -1;
    }
    free(client_handle);
    return HTTP_TEST_CLIENT_OK;
}

// Maximum buffer size for receiving HTTP headers
#define HTTP_RECV_BUFFER_SIZE 4096

http_test_client_err_t http_test_client_send_request(http_test_client_handle_t *client_handle,
                                                     http_method_t method,
                                                     const char *uri,
                                                     const char *headers_str,
                                                     const char *body, size_t body_len,
                                                     http_test_response_t *response,
                                                     uint32_t timeout_ms) {
    if (client_handle == NULL || uri == NULL || response == NULL) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }
    if (client_handle->sockfd == -1) {
        return HTTP_TEST_CLIENT_ERR_CONNECT; // Not connected
    }

    // Initialize response structure
    memset(response, 0, sizeof(http_test_response_t));

    // 1. Construct the HTTP request string
    char request_line[CONFIG_HTTPD_MAX_URI_LEN + 256];
    const char *method_str;
    switch (method) {
        case HTTP_METHOD_GET:
            method_str = "GET";
            break;
        case HTTP_METHOD_POST:
            method_str = "POST";
            break;
        default:
            return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }
    snprintf(request_line, sizeof(request_line), "%s %s HTTP/1.1\r\n", method_str, uri);

    // Calculate total request size
    size_t request_len = strlen(request_line);
    if (headers_str) {
        request_len += strlen(headers_str);
    }
    if (method == HTTP_METHOD_POST && body && body_len > 0) {
        char content_length_hdr[64];
        snprintf(content_length_hdr, sizeof(content_length_hdr), "Content-Length: %zu\r\n", body_len);
        request_len += strlen(content_length_hdr);
    }

    // Conditionally add Connection: close
    bool is_websocket_upgrade = (headers_str != NULL && strstr(headers_str, "Upgrade: websocket") != NULL);
    if (!is_websocket_upgrade) {
        request_len += strlen("Connection: close\r\n");
    }
    request_len += strlen("\r\n"); // End of headers

    char *full_request = (char*) malloc(request_len + 1);
    if (!full_request) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }
    full_request[0] = '\0';

    strcat(full_request, request_line);
    if (headers_str) {
        strcat(full_request, headers_str);
    }
    if (method == HTTP_METHOD_POST && body && body_len > 0) {
        char content_length_hdr[64];
        snprintf(content_length_hdr, sizeof(content_length_hdr), "Content-Length: %zu\r\n", body_len);
        strcat(full_request, content_length_hdr);
    }
    if (!is_websocket_upgrade) {
        strcat(full_request, "Connection: close\r\n");
    }
    strcat(full_request, "\r\n"); // End of headers

    // 2. Send the request over the socket
    http_test_client_err_t err = send_data(client_handle->sockfd, full_request, strlen(full_request), timeout_ms);
    free(full_request);
    if (err != HTTP_TEST_CLIENT_OK) {
        return err;
    }
    if (method == HTTP_METHOD_POST && body && body_len > 0) {
        err = send_data(client_handle->sockfd, body, body_len, timeout_ms);
        if (err != HTTP_TEST_CLIENT_OK) {
            return err;
        }
    }

    // 3. Receive the HTTP response
    char recv_buf[HTTP_RECV_BUFFER_SIZE] = {0};
    size_t total_recv = 0;
    int bytes_read;
    char *header_end = NULL;

    // Read until we find the end of headers (\r\n\r\n)
    while (header_end == NULL && total_recv < HTTP_RECV_BUFFER_SIZE) {
        bytes_read = recv_data(client_handle->sockfd, recv_buf + total_recv, HTTP_RECV_BUFFER_SIZE - total_recv - 1, timeout_ms);
        if (bytes_read == -1) {
            return HTTP_TEST_CLIENT_ERR_RECV;
        }
        if (bytes_read == 0) { // Timeout or connection closed
            if (total_recv == 0) { // No data received at all
                return HTTP_TEST_CLIENT_ERR_TIMEOUT;
            }
            break; // Connection closed after some data, try to parse what we have
        }
        LOGD_BUFFER_HEXDUMP(TAG,recv_buf + total_recv, bytes_read, "received");
        total_recv += bytes_read;
        recv_buf[total_recv] = '\0'; // Null-terminate for strstr
        header_end = strstr(recv_buf, "\r\n\r\n");
        // printf("found %s\r\n", header_end);
        // printf("total %d header %s\r\n", total_recv, header_end);
    }

    LOGD_BUFFER_HEXDUMP(TAG,recv_buf, total_recv, "done %p", header_end);

    if (header_end == NULL) {
        LOGD(TAG, "Header not found");
        return HTTP_TEST_CLIENT_ERR_PROTOCOL; // Did not find end of headers
    }

    // 4. Parse the response (status line, headers)
    *header_end = '\0'; // Null-terminate headers part
    char *body_start = header_end + 4; // Point to start of body

    // Parse status line
    char *status_line_end = strstr(recv_buf, "\r\n");
    if (!status_line_end) {
        LOGD(TAG, "Header not terminated");
        return HTTP_TEST_CLIENT_ERR_PROTOCOL;
    }
    *status_line_end = '\0';

    char *http_version = strtok(recv_buf, " ");
    char *status_code_str = strtok(NULL, " ");
    char *status_text_ptr = strtok(NULL, "\0"); // Rest of the line is status text

    if (!http_version || !status_code_str || !status_text_ptr) {
        LOGD(TAG, "Could not find version, code or status");
        return HTTP_TEST_CLIENT_ERR_PROTOCOL;
    }

    response->status_code = atoi(status_code_str);
    strncpy(response->status_text, status_text_ptr, sizeof(response->status_text) - 1);
    response->status_text[sizeof(response->status_text) - 1] = '\0';

    // Copy headers
    response->headers = strdup(status_line_end + 2); // Skip "\r\n" after status line
    if (!response->headers) {
        return HTTP_TEST_CLIENT_ERR_GENERIC;
    }

    // Determine Content-Length
    size_t content_length = 0;
    char *content_length_hdr = strstr(response->headers, "Content-Length:");
    if (content_length_hdr) {
        content_length = atoi(content_length_hdr + strlen("Content-Length:"));
    }

    // 5. Read response body
    size_t header_len = (body_start - recv_buf);
    size_t body_bytes_in_buf = total_recv - header_len;

    if (content_length > 0) {
        LOGD(TAG, "Allocating content length %d", content_length);
        response->body = (char*) malloc(content_length + 1);
        if (!response->body) {
            http_test_client_free_response(response);
            return HTTP_TEST_CLIENT_ERR_GENERIC;
        }
        response->body_len = content_length;

        // Copy body data already in buffer
        memcpy(response->body, body_start, body_bytes_in_buf);

        // Read remaining body data
        size_t total_body_read = body_bytes_in_buf;

        while (total_body_read < content_length) {
            bytes_read = recv(client_handle->sockfd, response->body + total_body_read, content_length - total_body_read, 0);
            if (bytes_read == -1) {
                http_test_client_free_response(response);
                LOGD(TAG, "Error reading response body: %s", strerror(errno));
                return HTTP_TEST_CLIENT_ERR_RECV;
            }
            if (bytes_read == 0) {
                // Connection closed prematurely
                http_test_client_free_response(response);
                LOGD(TAG, "Connection closed prematurely while reading body. Expected %d, received %d", content_length, total_body_read);
                return HTTP_TEST_CLIENT_ERR_PROTOCOL;
            }
            total_body_read += bytes_read;
            LOGD(TAG, "Read %d bytes, total %d/%d", bytes_read, total_body_read, content_length);
        }
        response->body[response->body_len] = '\0'; // Null-terminate body
    } else {
        // No Content-Length or 0, body is what's left in the buffer
        if (body_bytes_in_buf > 0) {
            response->body = (char*) malloc(body_bytes_in_buf + 1);
            if (!response->body) {
                http_test_client_free_response(response);
                return HTTP_TEST_CLIENT_ERR_GENERIC;
            }
            memcpy(response->body, body_start, body_bytes_in_buf);
            response->body_len = body_bytes_in_buf;
            response->body[response->body_len] = '\0';
        }
    }

    return HTTP_TEST_CLIENT_OK;
}

void http_test_client_free_response(http_test_response_t *response) {
    if (response) {
        free(response->headers);
        free(response->body);
        memset(response, 0, sizeof(http_test_response_t)); // Clear structure
    }
}


http_test_client_err_t ws_test_client_handshake(http_test_client_handle_t *client_handle,
                                                const char *uri,
                                                const char *host,
                                                const char *client_key,
                                                const char *expected_accept_key,
                                                uint32_t timeout_ms) {
    if (client_handle == NULL || uri == NULL || host == NULL || client_key == NULL || expected_accept_key == NULL) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }
    if (client_handle->sockfd == -1) {
        return HTTP_TEST_CLIENT_ERR_CONNECT; // Not connected
    }

    char headers_str[512];
    snprintf(headers_str, sizeof(headers_str),
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n",
             host, client_key);

    http_test_response_t response;
    http_test_client_err_t err = http_test_client_send_request(client_handle,
                                                               HTTP_METHOD_GET,
                                                               uri,
                                                               headers_str,
                                                               NULL, 0,
                                                               &response,
                                                               timeout_ms);
    if (err != HTTP_TEST_CLIENT_OK) {
        http_test_client_free_response(&response);
        return err;
    }

    if (response.status_code != 101) {
        http_test_client_free_response(&response);
        return HTTP_TEST_CLIENT_ERR_PROTOCOL; // Not 101 Switching Protocols
    }

    // Verify Sec-WebSocket-Accept header
    const char *received_accept_key_dyn = http_test_client_get_header(&response, "Sec-WebSocket-Accept");
    if (!received_accept_key_dyn) {
        http_test_client_free_response(&response);
        return HTTP_TEST_CLIENT_ERR_PROTOCOL; // Missing Sec-WebSocket-Accept
    }

    if (strcmp(received_accept_key_dyn, expected_accept_key) != 0) {
        free((void*)received_accept_key_dyn);
        http_test_client_free_response(&response);
        return HTTP_TEST_CLIENT_ERR_PROTOCOL; // Mismatched Sec-WebSocket-Accept
    }
    free((void*)received_accept_key_dyn);

    http_test_client_free_response(&response);
    return HTTP_TEST_CLIENT_OK;
}

http_test_client_err_t ws_test_client_send_frame(http_test_client_handle_t *client_handle,
                                                 const ws_test_frame_t *frame,
                                                 uint32_t timeout_ms) {
    if (client_handle == NULL || frame == NULL || (frame->payload_len > 0 && frame->payload == NULL)) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }
    if (client_handle->sockfd == -1) {
        return HTTP_TEST_CLIENT_ERR_CONNECT; // Not connected
    }

    uint8_t header[10]; // Max header size for 64-bit length + mask
    size_t header_len = 0;

    // Byte 0: FIN + RSV + Opcode
    header[header_len++] = (frame->fin ? 0x80 : 0x00) | frame->type;

    // Byte 1: Mask + Payload Length
    uint8_t mask_bit = frame->masked ? 0x80 : 0x00;
    if (frame->payload_len <= 125) {
        header[header_len++] = mask_bit | (uint8_t)frame->payload_len;
    } else if (frame->payload_len <= 65535) {
        header[header_len++] = mask_bit | 126;
        header[header_len++] = (frame->payload_len >> 8) & 0xFF;
        header[header_len++] = frame->payload_len & 0xFF;
    } else {
        header[header_len++] = mask_bit | 127;
        // 64-bit length (network byte order)
        header[header_len++] = (frame->payload_len >> 56) & 0xFF;
        header[header_len++] = (frame->payload_len >> 48) & 0xFF;
        header[header_len++] = (frame->payload_len >> 40) & 0xFF;
        header[header_len++] = (frame->payload_len >> 32) & 0xFF;
        header[header_len++] = (frame->payload_len >> 24) & 0xFF;
        header[header_len++] = (frame->payload_len >> 16) & 0xFF;
        header[header_len++] = (frame->payload_len >> 8) & 0xFF;
        header[header_len++] = frame->payload_len & 0xFF;
    }

    // Masking key
    if (frame->masked) {
        // For simplicity, use a fixed mask key for now, or generate random
        // For proper testing, a random key should be generated
        // For now, we'll use the one provided in the frame struct
        memcpy(&header[header_len], frame->mask, 4);
        header_len += 4;
    }

    // Send header
    http_test_client_err_t err = send_data(client_handle->sockfd, (const char*)header, header_len, timeout_ms);
    if (err != HTTP_TEST_CLIENT_OK) {
        return err;
    }

    // Send masked payload
    if (frame->payload_len > 0) {
        uint8_t *masked_payload = (uint8_t*) malloc(frame->payload_len);
        if (!masked_payload) {
            return HTTP_TEST_CLIENT_ERR_GENERIC;
        }
        for (size_t i = 0; i < frame->payload_len; i++) {
            masked_payload[i] = frame->payload[i] ^ frame->mask[i % 4];
        }
        err = send_data(client_handle->sockfd, (const char*)masked_payload, frame->payload_len, timeout_ms);
        free(masked_payload);
        if (err != HTTP_TEST_CLIENT_OK) {
            return err;
        }
    }

    return HTTP_TEST_CLIENT_OK;
}

http_test_client_err_t ws_test_client_recv_frame(http_test_client_handle_t *client_handle,
                                                 ws_test_frame_t *frame,
                                                 uint32_t timeout_ms) {
    if (client_handle == NULL || frame == NULL) {
        return HTTP_TEST_CLIENT_ERR_INVALID_ARG;
    }
    if (client_handle->sockfd == -1) {
        return HTTP_TEST_CLIENT_ERR_CONNECT; // Not connected
    }

    memset(frame, 0, sizeof(ws_test_frame_t));

    uint8_t header_buf[10]; // Max header size
    int bytes_read;
    size_t total_header_bytes = 0;

    // Read first two bytes of header
    bytes_read = recv_data(client_handle->sockfd, (char*)header_buf, 2, timeout_ms);
    if (bytes_read <= 0) {
        return HTTP_TEST_CLIENT_ERR_RECV;
    }
    total_header_bytes += bytes_read;

    frame->fin = (header_buf[0] & 0x80) != 0;
    frame->type = (ws_frame_type_t)(header_buf[0] & 0x0F);
    frame->masked = (header_buf[1] & 0x80) != 0;

    size_t payload_len_indicator = header_buf[1] & 0x7F;

    if (payload_len_indicator <= 125) {
        frame->payload_len = payload_len_indicator;
    } else if (payload_len_indicator == 126) {
        bytes_read = recv_data(client_handle->sockfd, (char*)header_buf + total_header_bytes, 2, timeout_ms);
        if (bytes_read <= 0) {
            return HTTP_TEST_CLIENT_ERR_RECV;
        }
        total_header_bytes += bytes_read;
        frame->payload_len = (header_buf[2] << 8) | header_buf[3];
    } else { // payload_len_indicator == 127
        bytes_read = recv_data(client_handle->sockfd, (char*)header_buf + total_header_bytes, 8, timeout_ms);
        if (bytes_read <= 0) {
            return HTTP_TEST_CLIENT_ERR_RECV;
        }
        total_header_bytes += bytes_read;
        frame->payload_len = ((uint64_t)header_buf[2] << 56) |
                             ((uint64_t)header_buf[3] << 48) |
                             ((uint64_t)header_buf[4] << 40) |
                             ((uint64_t)header_buf[5] << 32) |
                             ((uint64_t)header_buf[6] << 24) |
                             ((uint64_t)header_buf[7] << 16) |
                             ((uint64_t)header_buf[8] << 8) |
                             header_buf[9];
    }

    if (frame->masked) {
        bytes_read = recv_data(client_handle->sockfd, (char*)frame->mask, 4, timeout_ms);
        if (bytes_read <= 0) {
            return HTTP_TEST_CLIENT_ERR_RECV;
        }
    }

    if (frame->payload_len > 0) {
        frame->payload = (uint8_t*) malloc(frame->payload_len + 1); // +1 for null terminator for text frames
        if (!frame->payload) {
            return HTTP_TEST_CLIENT_ERR_GENERIC;
        }
        size_t total_payload_recv = 0;
        while (total_payload_recv < frame->payload_len) {
            bytes_read = recv_data(client_handle->sockfd, (char*)frame->payload + total_payload_recv, frame->payload_len - total_payload_recv, timeout_ms);
            if (bytes_read <= 0) {
                free(frame->payload);
                frame->payload = NULL;
                return HTTP_TEST_CLIENT_ERR_RECV;
            }
            total_payload_recv += bytes_read;
        }

        if (frame->masked) {
            for (size_t i = 0; i < frame->payload_len; i++) {
                frame->payload[i] ^= frame->mask[i % 4];
            }
        }
        frame->payload[frame->payload_len] = '\0'; // Null-terminate for text frames
    }

    return HTTP_TEST_CLIENT_OK;
}

void ws_test_client_free_frame(ws_test_frame_t *frame) {
    if (frame) {
        free(frame->payload);
        memset(frame, 0, sizeof(ws_test_frame_t));
    }
}

// Helper for case-insensitive string comparison
static int strncasecmp_custom(const char *s1, const char *s2, size_t n) {
    if (n == 0) {
        return 0;
    }

    while (n-- > 0) {
        unsigned char c1 = tolower((unsigned char)*s1);
        unsigned char c2 = tolower((unsigned char)*s2);

        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') { // Both strings ended and were equal up to this point
            return 0;
        }
        s1++;
        s2++;
    }
    return 0; // n characters compared and found equal
}

const char* http_test_client_get_header(const http_test_response_t *response, const char *header_name) {
    if (!response || !response->headers || !header_name) {
        return NULL;
    }

    size_t name_len = strlen(header_name);
    char *current_pos = response->headers;

    while (current_pos && *current_pos != '\0') {
        // Check if the current line starts with the header_name (case-insensitive)
        // and is followed by a colon

        if (strncasecmp_custom( header_name,current_pos, name_len) == 0 &&
            current_pos[name_len] == ':') {
            
            // Found the header, now extract the value
            char *value_start = current_pos + name_len + 1; // Skip "Header-Name:"
            while (*value_start == ' ' || *value_start == '\t') { // Skip leading whitespace
                value_start++;
            }

            // Find the end of the value (before \r\n)
            char *value_end = strstr(value_start, "\r\n");
            size_t value_len;
            if (value_end) {
                value_len = value_end - value_start;
            } else {
                value_len = strlen(value_start); // Last header, no \r\n
            }

            // Allocate memory for the value and copy it
            char *header_value = (char*) malloc(value_len + 1);
            if (!header_value) {
                return NULL; // Memory allocation failed
            }
            strncpy(header_value, value_start, value_len);
            header_value[value_len] = '\0';
            return header_value;
        }

        // Move to the next line
        current_pos = strstr(current_pos, "\r\n");
        if (current_pos) {
            current_pos += 2; // Skip "\r\n"
        } else {
            break; // No more lines
        }
    }

    return NULL; // Header not found
}
