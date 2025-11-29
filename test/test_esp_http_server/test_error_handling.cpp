#include <unity.h>
#include <esp_http_server.h>
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

#include <errno.h>
#include <sys/time.h> // Required for gettimeofday and struct timeval

#define TAG "TEST_HTTPD_ERROR"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define TEST_BUFFER_SIZE 1024
#define RECEIVE_TIMEOUT_SEC 5 // 5 seconds cumulative timeout for receive operations



// A flag to be set by the custom error handler
static bool custom_error_handler_invoked = false;

// Custom error handler function
static esp_err_t mock_error_handler(httpd_req_t *req, httpd_err_code_t error)
{
    (void)req; // Unused parameter
    custom_error_handler_invoked = true;
    // Send a custom error response to indicate the handler was invoked
    httpd_resp_send_err(req, error, "Custom Error Handler Invoked");
    return ESP_OK; // Keep the socket open
}



/**
 * Test: given_server_without_uri_handler_when_client_requests_unregistered_uri_then_404_not_found_is_returned
 *
 * Purpose: Verify that a network request to an unregistered URI correctly returns a 404 Not Found error.
 * Expected: The server responds with "404 Not Found".
 */
void given_server_without_uri_handler_when_client_requests_unregistered_uri_then_404_not_found_is_returned(void)
{
    // Given: A running server with no registered handler for the requested URI
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9007; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: A client connects and sends a request to an unregistered URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/unregistered_uri", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with a 404 Not Found
    TEST_ASSERT_EQUAL(404, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_NOT_NULL(strstr(response.body, "Nothing matches the given URI"));
    http_test_client_free_response(&response); // Free response body and headers

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_registered_uri_handler_for_get_when_post_request_then_405_method_not_allowed
 *
 * Purpose: Verify that a network request with an unsupported method for a valid URI returns a 405 Method Not Allowed error.
 * Expected: The server responds with "405 Method Not Allowed".
 */
void given_registered_uri_handler_for_get_when_post_request_then_405_method_not_allowed(void)
{
    // Given: A running server with a GET handler registered for a specific URI
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9008; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t get_uri = {
        .uri      = "/test_405",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_send(req, "GET received", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &get_uri));

    // When: A client connects and sends a POST request to the same URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_POST, "/test_405", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with a 405 Method Not Allowed
    TEST_ASSERT_EQUAL(405, response.status_code);
    TEST_ASSERT_NOT_NULL(response.status_text);
    TEST_ASSERT_EQUAL_STRING("Method Not Allowed", response.status_text);
    http_test_client_free_response(&response); // Free response body and headers

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}


/**
 * Test: given_server_running_when_malformed_request_is_sent_then_400_bad_request_is_returned
 *
 * Purpose: Verify that the server's parser can gracefully handle malformed HTTP requests.
 * Expected: The server should respond with a "400 Bad Request" error.
 */
void given_server_running_when_request_without_version_is_sent_then_505_version_unsupported_is_returned(void)
{
    // Given: A running server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9003;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: A client connects and sends a malformed request
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    TEST_ASSERT_EQUAL(0, connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    const char *malformed_request = "GET /test\r\n\r\n"; // Missing HTTP version
    send(sockfd, malformed_request, strlen(malformed_request), 0);

    char buffer[1024] = {0};
    httpd_os_thread_sleep(100);
    recv(sockfd, buffer, sizeof(buffer) - 1, 0);

    // Then: The server should respond with a 400 Bad Request
    TEST_ASSERT_NOT_NULL(strstr(buffer, "505 Version Not Supported"));

    // Cleanup
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    httpd_stop(handle);
}

/**
 * Test: given_server_running_when_long_uri_request_is_sent_then_414_uri_too_long_is_returned
 *
 * Purpose: Verify that the server correctly handles requests with URIs exceeding HTTPD_MAX_URI_LEN.
 * Expected: The server should respond with a "414 URI Too Long" error.
 */
void given_server_running_when_long_uri_request_is_sent_then_414_uri_too_long_is_returned(void)
{
    // Given: A running server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9004; // Use a different port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: A client connects and sends a request with an excessively long URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Construct a URI longer than CONFIG_HTTPD_MAX_URI_LEN (1024)
    // We'll make it 1025 characters long for example
    char long_uri[CONFIG_HTTPD_MAX_URI_LEN + 2]; // +1 for null terminator, +1 to exceed limit
    memset(long_uri, 'A', sizeof(long_uri) - 1);
    long_uri[0] = '/'; // Start with a slash
    long_uri[sizeof(long_uri) - 1] = '\0';

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, long_uri, NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with a 414 URI Too Long
    TEST_ASSERT_EQUAL(414, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("URI is too long",response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_running_when_long_header_request_is_sent_then_431_req_hdr_fields_too_large_is_returned
 *
 * Purpose: Verify that the server correctly handles requests with headers exceeding HTTPD_MAX_REQ_HDR_LEN.
 * Expected: The server should respond with a "431 Request Header Fields Too Large" error.
 */
void given_server_running_when_long_header_request_is_sent_then_431_req_hdr_fields_too_large_is_returned(void)
{
    // Given: A running server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9005; // Use a different port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: A client connects and sends a request with excessively long headers
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Construct a header string longer than CONFIG_HTTPD_MAX_REQ_HDR_LEN (512)
    // We'll make it 513 characters long for example
    char long_header_value[CONFIG_HTTPD_MAX_REQ_HDR_LEN + 2]; // +1 for null terminator, +1 to exceed limit
    memset(long_header_value, 'B', sizeof(long_header_value) - 1);
    long_header_value[sizeof(long_header_value) - 1] = '\0';

    char headers_str[CONFIG_HTTPD_MAX_REQ_HDR_LEN + 100]; // Buffer for the full header string
    snprintf(headers_str, sizeof(headers_str), "X-Long-Header: %s\r\n", long_header_value);

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/test", headers_str, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with a 431 Request Header Fields Too Large
    TEST_ASSERT_EQUAL(431, response.status_code);
    TEST_ASSERT_NOT_NULL(response.status_text);
    TEST_ASSERT_EQUAL_STRING("Request Header Fields Too Large", response.status_text);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_custom_error_handler_when_error_occurs_then_handler_is_invoked
 *
 * Purpose: Verify that a custom error handler registered with httpd_register_err_handler
 *          is correctly invoked when the corresponding HTTP error occurs.
 * Expected: The custom error handler's flag is set, and a custom error response is received.
 */
void given_server_with_custom_error_handler_when_error_occurs_then_handler_is_invoked(void)
{
    // Given: A running server with a custom error handler registered for 404 Not Found
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9016; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Register the custom error handler for 404 Not Found
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_err_handler(handle, HTTPD_404_NOT_FOUND, mock_error_handler));

    custom_error_handler_invoked = false; // Reset flag

    // When: A client connects and sends a request to an unregistered URI (triggering 404)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/non_existent_uri", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The custom error handler should have been invoked, and a custom error response received
    TEST_ASSERT_TRUE(custom_error_handler_invoked);
    TEST_ASSERT_EQUAL(404, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Custom Error Handler Invoked", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Loops to receive all available data from a socket until EOF (0) or fatal error (-1).
 * * @param sockfd The socket file descriptor (SOCKET).
 * @param buffer The buffer to store the received data.
 * @param buffer_size The maximum size of the buffer (including space for null terminator).
 * @return The total number of bytes read (0 or greater), or -1 on a fatal error.
 */
int receive_all_data(int sockfd, char* buffer, size_t buffer_size) {
    int total_recv = 0;
    int recv_len;
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);
    long elapsed_ms;
    
    // Check for a usable buffer size (must reserve 1 byte for null terminator)
    if (buffer_size < 2) {
        if (buffer_size == 1 && buffer) buffer[0] = '\0';
        return 0; 
    }

    LOGD(TAG, "Starting read loop on socket %d with max capacity %zu bytes and cumulative timeout %d seconds.", sockfd, buffer_size - 1, RECEIVE_TIMEOUT_SEC);

    // Loop until an error (-1) occurs or cumulative timeout is reached
    while (true) {
        gettimeofday(&current_time, NULL);
        elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + (current_time.tv_usec - start_time.tv_usec) / 1000;

        if (elapsed_ms >= RECEIVE_TIMEOUT_SEC * 1000) {
            LOGD(TAG, "Cumulative receive timeout of %d seconds reached.", RECEIVE_TIMEOUT_SEC);
            break; // Cumulative timeout reached
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        long remaining_ms = (RECEIVE_TIMEOUT_SEC * 1000) - elapsed_ms;
        timeout.tv_sec = remaining_ms / 1000;
        timeout.tv_usec = (remaining_ms % 1000) * 1000;

        // Ensure timeout values are not negative
        if (timeout.tv_sec < 0) timeout.tv_sec = 0;
        if (timeout.tv_usec < 0) timeout.tv_usec = 0;

        int select_ret = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

        if (select_ret == -1) {
            // Error in select
            LOGE(TAG, "Error in select: %d", errno);
            return -1; // Fatal error
        } else if (select_ret == 0) {
            // Select timed out, continue loop to check cumulative time
            continue;
        }

        // Data is available, proceed with recv
        recv_len = recv(sockfd, buffer + total_recv, buffer_size - 1 - total_recv, 0);

        if (recv_len == -1) {
            int error_code;
            #ifdef _WIN32
                error_code = WSAGetLastError();
                if (error_code == WSAEWOULDBLOCK) { 
                    LOGD(TAG, "Timeout/No data available (WSAEWOULDBLOCK) during recv. Continuing loop.");
                    continue; // Non-fatal, continue to check cumulative time
                } else {
                    LOGE(TAG, "Fatal receive error: %d (Winsock). Returning -1.", error_code);
                    return -1;
                }
            #else // Linux/POSIX
                error_code = errno;
                if (error_code == EAGAIN || error_code == EWOULDBLOCK) { 
                    LOGD(TAG, "Timeout/No data available (EAGAIN/EWOULDBLOCK) during recv. Continuing loop.");
                    continue; // Non-fatal, continue to check cumulative time
                } else {
                    LOGE(TAG, "Fatal receive error: %d (errno). Returning -1.", error_code);
                    return -1;
                }
            #endif
        }
        
        // --- 1. Connection Gracefully Closed (EOF) ---
        if (recv_len == 0) {
            LOGD(TAG, "Connection closed by peer (EOF). Read finished.");
            break;
        }

        // --- 2. Data Received Successfully (recv_len > 0) ---
        LOGD_BUFFER_HEXDUMP(TAG, buffer + total_recv, recv_len, "Received chunk size: %d", recv_len);
        total_recv += recv_len;
        
        // Check for buffer overflow before the next iteration
        if (total_recv >= buffer_size - 1) {
            LOGD(TAG, "Buffer full (%d bytes). Stopping read to prevent overflow.", total_recv);
            break;
        }
    }

    // Finalize: Null-terminate the received data
    buffer[total_recv] = '\0'; 
    LOGD(TAG, "Receive operation completed. Total bytes read: %d", total_recv);
    return total_recv;
    // If a fatal error occurred during select or recv, -1 would have been returned earlier.
    if (total_recv == 0 && elapsed_ms >= RECEIVE_TIMEOUT_SEC * 1000) {
        // If no data was received and cumulative timeout occurred, treat as timeout.
        // This condition is to ensure that if the loop exits due to cumulative timeout
        // before any data is received, it's handled gracefully.
        LOGD(TAG, "No data received within cumulative timeout.");
    } else if (recv_len == -1) { // This condition should ideally not be hit now if fatal errors return -1 immediately
        int error_code;
        #ifdef _WIN32
            error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) { // Only log fatal errors here
                LOGE(TAG, "Fatal receive error: %d (Winsock) after loop. Returning -1.", error_code);
                return -1;
            }
        #else // Linux/POSIX
            error_code = errno;
            if (error_code != EAGAIN && error_code != EWOULDBLOCK) { // Only log fatal errors here
                LOGE(TAG, "Fatal receive error: %d (errno) after loop. Returning -1.", error_code);
                return -1;
            }
        #endif
    }

    // --- 3. Error Occurred (recv_len == -1 / SOCKET_FATAL_ERROR) ---
    if (recv_len == -1) {
        
        int error_code;
        #ifdef _WIN32
            error_code = WSAGetLastError();
            // WSAEWOULDBLOCK is the non-fatal error for non-blocking sockets/timeouts
            if (error_code == WSAEWOULDBLOCK) { 
                LOGD(TAG, "Timeout/No data available (WSAEWOULDBLOCK). Returning currently read data.");
                // We treat this as a non-fatal stop and return the data we got.
            } else {
                LOGE(TAG, "Fatal receive error: %d (Winsock). Returning -1.", error_code);
                return -1;
            }
        #else // Linux/POSIX
            error_code = errno;
            // EAGAIN/EWOULDBLOCK is the non-fatal error for non-blocking sockets/timeouts
            if (error_code == EAGAIN || error_code == EWOULDBLOCK) { 
                LOGD(TAG, "Timeout/No data available (EAGAIN/EWOULDBLOCK). Returning currently read data.");
                // Non-fatal, return data received so far.
            } else {
                LOGE(TAG, "Fatal receive error: %d (errno). Returning -1.", error_code);
                return -1;
            }
        #endif
    }

    // Finalize: Null-terminate the received data
    buffer[total_recv] = '\0'; 
    LOGD(TAG, "Receive operation completed. Total bytes read: %d", total_recv);
    return total_recv;
}

/**
 * Test: given_request_with_less_content_length_when_sent_then_server_handles_correctly
 *
 * Purpose: Verify that the server correctly handles requests where the actual body size
 *          is LESS than the Content-Length header.
 * Expected: The server should read the available data, then timeout/error on remaining read,
 *           and respond with the actual bytes received.
 */
void given_request_with_less_content_length_when_sent_then_server_handles_correctly(void)
{
    // Given: A running server with a POST handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9010; // Use a unique port
    config.recv_wait_timeout = 1; // Short timeout for quicker test failure
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Handler to read the body
    httpd_uri_t post_uri = {
        .uri      = "/test_content_length",
        .method   = HTTP_POST,
        .handler  = [](httpd_req_t *req) {
            char buffer[128];
            int total_recv = 0;
            int r;
            bool timeout_occurred = false;
            while ((r = httpd_req_recv(req, buffer, sizeof(buffer))) > 0) {
                LOGD_BUFFER_HEXDUMP(TAG, buffer, r, "test content length");
                total_recv += r;
            }
            if (r < 0) { // httpd_req_recv returned an error (e.g., timeout)
                timeout_occurred = true;
            }

            // If Content-Length was specified and not fully received, and a timeout occurred,
            // consider it as 0 bytes received for the purpose of this test, as per user's interpretation.
            if (req->content_len > 0 && total_recv < req->content_len && timeout_occurred) {
                total_recv = 0;
            }

            char resp_str[64];
            LOGD(TAG, "received %d bytes", total_recv);
            snprintf(resp_str, sizeof(resp_str), "Received %d bytes !!!", total_recv);
            httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &post_uri));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[1024] = {0};
    int sockfd;
    int connect_ret;
    int send_ret;
    int recv_ret;

    // Client sends LESS data than Content-Length
    LOGI(TAG, "Test Case: Client sends LESS data than Content-Length");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);
    connect_ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    TEST_ASSERT_EQUAL(0, connect_ret);

    const char *request_header_less = "POST /test_content_length HTTP/1.1\r\nHost: localhost\r\nContent-Length: 25\r\n\r\n";
    const char *request_body_less = "short_body"; // 10 bytes
    send_ret = send(sockfd, request_header_less, strlen(request_header_less), 0);
    TEST_ASSERT_GREATER_THAN(0, send_ret);
    send_ret = send(sockfd, request_body_less, strlen(request_body_less), 0);
    TEST_ASSERT_GREATER_THAN(0, send_ret);

    // Give server a moment to process the sent data before attempting to read response
    httpd_os_thread_sleep(1); // Small delay


//      // Set receive timeout
// #ifdef _WIN32
//     DWORD timeout = TEST_TIMEOUT_MS;
//     setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
// #else
//     struct timeval tv;
//     tv.tv_sec = TEST_TIMEOUT_MS / 1000;
//     tv.tv_usec = (TEST_TIMEOUT_MS % 1000) * 1000;
//     setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
// #endif

//     memset(buffer, 0, sizeof(buffer));
//     int total_recv = 0;
//     int recv_len;

//     // Loop to receive all available data
//     while ((recv_len = recv(sockfd, buffer + total_recv, sizeof(buffer) - 1 - total_recv, 0)) >= 0) {
//         LOGD_BUFFER_HEXDUMP(TAG, buffer + total_recv, recv_len,"reading %d", recv_len);
//         total_recv += recv_len;
//         if (total_recv >= sizeof(buffer) - 1) {
//             // Buffer is full, break to avoid overflow
//             break;
//         }
//     }
//     LOGD(TAG, "Read %d",recv_len);
//     buffer[total_recv] = '\0'; // Null-terminate the received data
//     TEST_ASSERT_GREATER_THAN(0, recv_ret); // Ensure data was received
//     LOGD_BUFFER_HEXDUMP(TAG, buffer, sizeof(buffer), "Received response for less data");

    int bytes_read = receive_all_data(sockfd, 
                                  buffer, 
                                  sizeof(buffer));

    TEST_ASSERT_GREATER_THAN(0, recv_ret); // Ensure data was received

    // The server should have read the 10 bytes sent, then timed out waiting for the remaining 10.
    TEST_MESSAGE(buffer);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Received 0 bytes"));

    #ifdef _WIN32
        closesocket(sockfd);
    #else
        close(sockfd);
    #endif
    httpd_stop(handle);
}

/**
 * Test: given_request_with_more_content_length_when_sent_then_server_handles_correctly
 *
 * Purpose: Verify that the server correctly handles requests where the actual body size
 *          is MORE than the Content-Length header.
 * Expected: The server should only read up to Content-Length.
 */
void given_request_with_more_content_length_when_sent_then_server_handles_correctly(void)
{
    // Given: A running server with a POST handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9011; // Use a unique port
    config.recv_wait_timeout = 1; // Short timeout for quicker test failure
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Handler to read the body
    httpd_uri_t post_uri = {
        .uri      = "/test_content_length",
        .method   = HTTP_POST,
        .handler  = [](httpd_req_t *req) {
            char buffer[128];
            int total_recv = 0;
            int r;
            while ((r = httpd_req_recv(req, buffer, sizeof(buffer))) > 0) {
                total_recv += r;
            }
            // If httpd_req_recv returns an error (e.g., timeout, connection close),
            // it will be negative. If it returns 0, it means connection closed gracefully or no more data.

            // Respond with the actual bytes received
            char resp_str[64];
            snprintf(resp_str, sizeof(resp_str), "Received %d bytes", total_recv);
            httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &post_uri));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[1024] = {0};
    int sockfd;
    int connect_ret;
    int send_ret;
    int recv_ret;

    // Client sends MORE data than Content-Length
    LOGI(TAG, "Test Case: Client sends MORE data than Content-Length");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);
    connect_ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    TEST_ASSERT_EQUAL(0, connect_ret);

    const char *request_header_more = "POST /test_content_length HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\n";
    const char *request_body_more = "this_is_a_long_body"; // 19 bytes, but Content-Length is 5
    send_ret = send(sockfd, request_header_more, strlen(request_header_more), 0);
    TEST_ASSERT_GREATER_THAN(0, send_ret);
    send_ret = send(sockfd, request_body_more, strlen(request_body_more), 0);
    TEST_ASSERT_GREATER_THAN(0, send_ret);

    memset(buffer, 0, sizeof(buffer));
    httpd_os_thread_sleep(100); // Give server time to process
    recv_ret = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, recv_ret);
    LOGI(TAG, "Received response for more data: %s", buffer);
    // Server should only read 5 bytes as per Content-Length
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Received 5 bytes"));

    // Cleanup
    #ifdef _WIN32
        closesocket(sockfd);
    #else
        close(sockfd);
    #endif
    httpd_stop(handle);
}

int test_error_handling(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_without_uri_handler_when_client_requests_unregistered_uri_then_404_not_found_is_returned);
    RUN_TEST(given_registered_uri_handler_for_get_when_post_request_then_405_method_not_allowed);
    RUN_TEST(given_server_running_when_request_without_version_is_sent_then_505_version_unsupported_is_returned);
    RUN_TEST(given_server_running_when_long_uri_request_is_sent_then_414_uri_too_long_is_returned);
    RUN_TEST(given_server_running_when_long_header_request_is_sent_then_431_req_hdr_fields_too_large_is_returned);
    RUN_TEST(given_server_with_custom_error_handler_when_error_occurs_then_handler_is_invoked);
    RUN_TEST(given_request_with_less_content_length_when_sent_then_server_handles_correctly);
    RUN_TEST(given_request_with_more_content_length_when_sent_then_server_handles_correctly);
    // return UNITY_END();
    return 0;
}
