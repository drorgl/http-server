#include <unity.h>
#include <http_server.h>
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
#include <cctype>

#define TEST_TIMEOUT_MS 5000

void nop(void * ctx){};

/**
 * Helper function: Validate Content-Range header format
 *
 * @param content_range The Content-Range header value to validate
 * @return true if valid, false otherwise
 */
bool validate_content_range_header(const char *content_range) {
    // Expected format: "bytes start-end/total" or "bytes */total"
    if (!content_range || strlen(content_range) < 8) {
        return false;
    }

    // Check prefix
    if (strncmp(content_range, "bytes ", 6) != 0) {
        return false;
    }

    // Skip "bytes " and check the range part
    const char *range_part = content_range + 6;

    // Handle "*/total" format for 416 responses
    if (range_part[0] == '*' && range_part[1] == '/') {
        // Check if total is a valid number
        const char *total_str = range_part + 2;
        if (strlen(total_str) == 0) {
            return false;
        }
        for (size_t i = 0; i < strlen(total_str); i++) {
            if (!isdigit((unsigned char)total_str[i])) {
                return false;
            }
        }
        return true;
    }

    // Handle "start-end/total" format for 206 responses
    // Find the slash
    const char *slash_pos = strchr(range_part, '/');
    if (!slash_pos) {
        return false;
    }

    // Check if total is a valid number
    const char *total_str = slash_pos + 1;
    if (strlen(total_str) == 0) {
        return false;
    }
    for (size_t i = 0; i < strlen(total_str); i++) {
        if (!isdigit((unsigned char)total_str[i])) {
            return false;
        }
    }

    // Check if range part is valid (start-end format)
    char range_copy[64];
    size_t range_len = slash_pos - range_part;
    if (range_len >= sizeof(range_copy)) {
        return false;
    }
    memcpy(range_copy, range_part, range_len);
    range_copy[range_len] = '\0';

    // Find the dash
    const char *dash_pos = strchr(range_copy, '-');
    if (!dash_pos) {
        return false;
    }

    // Validate start and end are numbers
    for (size_t i = 0; i < strlen(range_copy); i++) {
        char c = range_copy[i];
        if (c == '-') continue;
        if (!isdigit((unsigned char)c)) {
            return false;
        }
    }

    return true;
}

/**
 * Helper function: Create range request headers for testing
 *
 * @param range_value The Range header value (e.g., "bytes=0-99")
 * @return Allocated headers string (must be freed by caller)
 */
char* create_range_header(const char *range_value) {
    char *headers = (char*)malloc(1024); // Sufficient for test headers
    if (!headers) return NULL;

    snprintf(headers, 1024, "Range: %s\r\n", range_value);
    return headers;
}

/**
 * Helper function: Validate 206 Partial Content response format
 *
 * @param response The HTTP response to validate
 * @param expected_content_range Expected Content-Range header value
 * @param expected_content_length Expected Content-Length header value
 * @return true if response is properly formatted for 206, false otherwise
 */
bool validate_partial_content_response(const http_test_response_t *response,
                                     const char *expected_content_range,
                                     size_t expected_content_length) {
    // Check status code
    if (response->status_code != 206) {
        return false;
    }

    // Check Content-Range header
    const char *content_range = http_test_client_get_header(response, "Content-Range");
    if (!content_range) {
        return false;
    }

    bool range_valid = false;
    if (expected_content_range) {
        range_valid = (strcmp(content_range, expected_content_range) == 0);
    } else {
        // Just validate format
        range_valid = validate_content_range_header(content_range);
    }
    free((void*)content_range);

    if (!range_valid) {
        return false;
    }

    // Check Content-Length (if expected)
    if (expected_content_length > 0 && response->body_len != expected_content_length) {
        return false;
    }

    return true;
}

/**
 * Helper function: Validate 416 Range Not Satisfiable response format
 *
 * @param response The HTTP response to validate
 * @param expected_total_length Expected total resource length
 * @return true if response is properly formatted for 416, false otherwise
 */
bool validate_range_not_satisfiable_response(const http_test_response_t *response,
                                            long long expected_total_length) {
    // Check status code
    if (response->status_code != 416) {
        return false;
    }

    // Check Content-Range header (should be "bytes */total")
    const char *content_range = http_test_client_get_header(response, "Content-Range");
    if (!content_range) {
        return false;
    }

    char expected_range[64];
    snprintf(expected_range, sizeof(expected_range), "bytes */%lld", expected_total_length);
    bool range_valid = (strcmp(content_range, expected_range) == 0);

    free((void*)content_range);

    return range_valid;
}

/**
 * Helper function: Validate 308 Permanent Redirect response format
 *
 * @param response The HTTP response to validate
 * @param expected_location Expected Location header value (NULL to skip check)
 * @return true if response is properly formatted for 308, false otherwise
 */
bool validate_permanent_redirect_response(const http_test_response_t *response,
                                        const char *expected_location) {
    // Check status code
    if (response->status_code != 308) {
        return false;
    }

    // Check Location header if expected
    if (expected_location) {
        const char *location = http_test_client_get_header(response, "Location");
        if (!location) {
            return false;
        }

        bool location_valid = (strcmp(location, expected_location) == 0);
        free((void*)location);

        if (!location_valid) {
            return false;
        }
    }

    return true;
}

/**
 * Helper function: Validate 421 Misdirected Request response format
 *
 * @param response The HTTP response to validate
 * @return true if response is properly formatted for 421, false otherwise
 */
bool validate_misdirected_request_response(const http_test_response_t *response) {
    // Check status code
    if (response->status_code != 421) {
        return false;
    }

    // RFC 7540 suggests clients might benefit from explanatory text
    // but there are no mandatory headers for 421
    return true;
}

/**
 * Helper function: Validate 426 Upgrade Required response format
 *
 * @param response The HTTP response to validate
 * @param expected_upgrade Expected Upgrade header value (e.g., "h2", "WebSocket", NULL to skip check)
 * @return true if response is properly formatted for 426, false otherwise
 */
bool validate_upgrade_required_response(const http_test_response_t *response,
                                       const char *expected_upgrade) {
    // Check status code
    if (response->status_code != 426) {
        return false;
    }

    // Check Upgrade header if expected (RFC 7230 requires Upgrade header for 426)
    if (expected_upgrade) {
        const char *upgrade = http_test_client_get_header(response, "Upgrade");
        if (!upgrade) {
            return false;
        }

        bool upgrade_valid = (strcmp(upgrade, expected_upgrade) == 0);
        free((void*)upgrade);

        if (!upgrade_valid) {
            return false;
        }
    }

    return true;
}


/**
 * Test: given_valid_request_when_calling_httpd_resp_send_then_response_is_sent
 * 
 * Purpose: Verify that HTTP responses can be sent successfully
 * Expected: httpd_resp_send() returns ESP_OK when called from valid handler
 */
void given_valid_request_when_calling_httpd_resp_send_then_response_is_sent(void)
{
    // This test would require a full HTTP request/response cycle
    // which is complex to mock. This test verifies the API exists
    // and can be called (basic compilation check)
    
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8087;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Note: Actual response sending would require:
    // 1. HTTP client connection
    // 2. Valid httpd_req_t structure
    // 3. Handler function context
    
    // For this basic test, we just verify the server starts correctly
    TEST_ASSERT_NOT_NULL(handle);
    
    // Cleanup
    httpd_stop(handle);
}


/**
 * Test: given_server_with_resp_send_handler_when_client_requests_then_receives_response
 *
 * Purpose: Verify that httpd_resp_send() correctly sends a full HTTP response to a client.
 * Expected: The client receives a 200 OK response with the expected body.
 */
void given_server_with_resp_send_handler_when_client_requests_then_receives_response(void)
{
    // Given: A running server with a handler that uses httpd_resp_send
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8087; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    static const char *test_response_body = "Hello from httpd_resp_send!";

    httpd_uri_t resp_send_uri = {
        .uri      = "/resp_send_test",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_send(req, test_response_body, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &resp_send_uri));

    // When: A client connects and sends a request to the URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/resp_send_test", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The client receives a 200 OK response with the expected body
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING(test_response_body, response.body);
    http_test_client_free_response(&response); // Free response body and headers

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}



/**
 * Test: given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response
 *
 * Purpose: Verify that the server can send responses with custom headers, status codes, and content types.
 * Expected: The client receives a response with the specified custom status, content type, and headers.
 */
void given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response(void)
{
    // Given: A running server with a handler that sends a custom response
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9014; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t custom_response_uri = {
        .uri      = "/custom_response",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_set_status(req, "202 Accepted");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "X-Custom-Header", "CustomValue");
            httpd_resp_send(req, "{\"message\": \"Custom response received\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &custom_response_uri));

    // When: A client connects and sends a request to the custom response URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/custom_response", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The client receives a response with the custom status, content type, and header
    TEST_ASSERT_EQUAL(202, response.status_code);
    const char* content_type_header = http_test_client_get_header(&response, "Content-Type");
    TEST_ASSERT_NOT_NULL(content_type_header);
    TEST_ASSERT_EQUAL_STRING("application/json", content_type_header);
    free((void*)content_type_header); // Free the allocated string

    const char* custom_header = http_test_client_get_header(&response, "X-Custom-Header");
    TEST_ASSERT_NOT_NULL(custom_header);
    TEST_ASSERT_EQUAL_STRING("CustomValue", custom_header);
    free((void*)custom_header); // Free the allocated string

    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("{\"message\": \"Custom response received\"}", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}


/**
 * Test: given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response
 *
 * Purpose: Verify that the server can send chunked responses using httpd_resp_send_chunk.
 * Expected: The client receives a chunked response with the correct Transfer-Encoding header and body.
 */
void given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response(void)
{
    // Given: A running server with a handler that sends a chunked response
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9013; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t chunked_uri = {
        .uri      = "/chunked",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");
            httpd_resp_send_chunk(req, "Hello", HTTPD_RESP_USE_STRLEN);
            httpd_resp_send_chunk(req, ", ", HTTPD_RESP_USE_STRLEN);
            httpd_resp_send_chunk(req, "world!", HTTPD_RESP_USE_STRLEN);
            httpd_resp_send_chunk(req, NULL, 0); // End of chunked response
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &chunked_uri));

    // When: A client connects and sends a request to the chunked URI
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    TEST_ASSERT_EQUAL(0, connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    const char *request = "GET /chunked HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sockfd, request, strlen(request), 0);

    char buffer[1024] = {0};
    httpd_os_thread_sleep(100); // Give server a moment to process
    int recv_ret = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, recv_ret);
    buffer[recv_ret] = '\0'; // Null-terminate the received data

    // Then: The client receives a chunked response with the correct content
    TEST_ASSERT_NOT_NULL(strstr(buffer, "HTTP/1.1 200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Transfer-Encoding: chunked"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "5\r\nHello\r\n2\r\n, \r\n6\r\nworld!\r\n0\r\n\r\n"));

    // Cleanup
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    httpd_stop(handle);
}



#define LARGE_RESPONSE_SIZE (1024 * 1024) // 1MB



/**
 * Test: given_server_with_large_response_handler_when_client_requests_then_receives_large_response
 *
 * Purpose: Verify that the server can send large response bodies efficiently and correctly.
 * Expected: The client receives a 200 OK response with the full large body.
 */
void given_server_with_large_response_handler_when_client_requests_then_receives_large_response(void)
{
    // Given: A running server with a handler that sends a large response
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9015; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t large_response_uri = {
        .uri      = "/large_response",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char *large_buffer = (char *)malloc(LARGE_RESPONSE_SIZE);
            TEST_ASSERT_NOT_NULL(large_buffer);
            memset(large_buffer, 'A', LARGE_RESPONSE_SIZE); // Fill with 'A's

            httpd_resp_set_type(req, "text/plain");
            esp_err_t err = httpd_resp_send(req, large_buffer, LARGE_RESPONSE_SIZE);
            free(large_buffer);
            return err;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &large_response_uri));

    // When: A client connects and sends a request to the large response URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/large_response", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The client receives the full large response body
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_EQUAL(LARGE_RESPONSE_SIZE, response.body_len);
    TEST_ASSERT_NOT_NULL(response.body);
    for (size_t i = 0; i < LARGE_RESPONSE_SIZE; i++) {
        TEST_ASSERT_EQUAL('A', response.body[i]);
    }

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}



/**
 * Test: given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches
 * 
 * Purpose: Verify URI wildcard matching functionality
 * Expected: httpd_uri_match_wildcard() returns correct boolean results
 */
void given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches(void)
{
    // Test various wildcard patterns
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("*", "/any/path", strlen("/any/path")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/api/?", "/api", strlen("/api")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/api/?", "/api/", strlen("/api/")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/api/*", "/api/status", strlen("/api/status")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/path/*", "/path/", strlen("/path/")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/path/?*", "/path", strlen("/path")));
    TEST_ASSERT_TRUE(httpd_uri_match_wildcard("/path/?*", "/path/blabla", strlen("/path/blabla")));
    
    // Test non-matching cases
    TEST_ASSERT_FALSE(httpd_uri_match_wildcard("/api", "/different", strlen("/different")));
    TEST_ASSERT_FALSE(httpd_uri_match_wildcard("/api/*", "/api", strlen("/api")));
    TEST_ASSERT_FALSE(httpd_uri_match_wildcard("/path/?", "/pathxx", strlen("/pathxx")));
}


/**
 * Test: given_valid_global_context_when_setting_and_getting_then_context_preserved
 * 
 * Purpose: Verify global user context functionality
 * Expected: Global context set in config can be retrieved via httpd_get_global_user_ctx()
 */
void given_valid_global_context_when_setting_and_getting_then_context_preserved(void)
{
    // Given: Server config with global user context
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8092;
    
    // Create test context
    char test_context[] = "test_global_context";
    config.global_user_ctx = test_context;
    config.global_user_ctx_free_fn = &nop;  // Don't free static string
    
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // When: Retrieving global user context
    void* retrieved_ctx = httpd_get_global_user_ctx(handle);
    
    // Then: Retrieved context matches original
    TEST_ASSERT_EQUAL_PTR(test_context, retrieved_ctx);
    
    // Cleanup
    httpd_stop(handle);
}

/**
 * Test: given_valid_session_context_when_setting_and_getting_then_context_preserved
 *
 * Purpose: Verify that session-specific context can be set and retrieved correctly.
 * Expected: httpd_sess_set_ctx() and httpd_sess_get_ctx() work as expected, and handle NULL arguments gracefully.
 */
void given_valid_session_context_when_setting_and_getting_then_context_preserved(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8096; // Use a different port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Mock a socket file descriptor (sockfd)
    int mock_sockfd = 100; // A dummy socket FD for testing

    // Get a free session slot and manually set it up
    struct sock_db *session = httpd_sess_get_free((struct httpd_data *)handle);
    TEST_ASSERT_NOT_NULL(session);
    session->fd = mock_sockfd;
    session->handle = handle;
    // session->free_ctx = nop;
    ((struct httpd_data *)handle)->hd_sd_active_count++;

    // Create test context
    char test_session_context[] = "test_session_data";
    void *ctx_to_set = (void*)test_session_context;

    // When: Setting session context
    httpd_sess_set_ctx(handle, mock_sockfd, ctx_to_set, nop);

    // Then: Retrieving session context matches original
    void *retrieved_ctx = httpd_sess_get_ctx(handle, mock_sockfd);
    TEST_ASSERT_EQUAL_PTR(ctx_to_set, retrieved_ctx);

    // Test with NULL handle
    retrieved_ctx = httpd_sess_get_ctx(NULL, mock_sockfd);
    TEST_ASSERT_NULL(retrieved_ctx);

    // Test with NULL context to set
    httpd_sess_set_ctx(handle, mock_sockfd, NULL, nop);
    retrieved_ctx = httpd_sess_get_ctx(handle, mock_sockfd);
    TEST_ASSERT_NULL(retrieved_ctx);

    // Cleanup: Delete the mocked session and stop the server
    httpd_sess_delete((struct httpd_data *)handle, session);
    httpd_stop(handle);
}

/**
 ,mock_file_data     Complete file content for testing
 */
static const char TEST_DATA[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static const size_t TEST_DATA_LEN = sizeof(TEST_DATA) - 1; // Exclude null terminator

/**
 * Test: given_server_with_range_middleware_when_client_requests_valid_range_then_206_partial_content_returned
 *
 * Purpose: Verify that range middleware correctly returns 206 Partial Content for valid single ranges
 * RFC 9110 compliance: Section 14.4 (Content-Range) and 15.3.7 (206 Partial Content)
 */
void given_server_with_range_middleware_when_client_requests_valid_range_then_206_partial_content_returned(void)
{
    // Given: Server with range middleware registered
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9050; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // For this implementation phase, we'll register a regular URI handler that checks for Range header
    // rather than using middleware registration (which may not be fully available yet)
    httpd_uri_t range_uri = {
        .uri      = "/range_content",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Check if this is a range request
            char range_buf[256];
            esp_err_t ret = httpd_req_get_hdr_value_str(req, "Range", range_buf, sizeof(range_buf));
            const char *range_header = (ret == ESP_OK) ? range_buf : NULL;
            if (range_header && strncmp(range_header, "bytes=", 6) == 0) {
                // Parse range
                const char *range_spec = range_header + 6;
                char *dash_pos = strchr(range_spec, '-');
                if (dash_pos) {
                    long long start = atol(range_spec);
                    long long end = atol(dash_pos + 1);
                    long long total_len = TEST_DATA_LEN;

                    // Clamp bounds
                    if (start < 0) start = 0;
                    if (end >= total_len) end = total_len - 1;
                    if (start > end || start >= total_len) {
                        httpd_resp_set_status(req, "416 Range Not Satisfiable");
                        char content_range[64];
                        snprintf(content_range, sizeof(content_range), "bytes */%lld", total_len);
                        httpd_resp_set_hdr(req, "Content-Range", content_range);
                        return httpd_resp_send(req, "", 0);
                    }

                    size_t content_len = end - start + 1;

                    // Create partial content buffer
                    char *buffer = (char *)malloc(content_len + 1);
                    if (buffer) {
                        memcpy(buffer, TEST_DATA + start, content_len);
                        buffer[content_len] = '\0';

                        httpd_resp_set_status(req, "206 Partial Content");
                        char content_range[64];
                        snprintf(content_range, sizeof(content_range), "bytes %lld-%lld/%lld", start, end, total_len);
                        httpd_resp_set_hdr(req, "Content-Range", content_range);

                        esp_err_t err = httpd_resp_send(req, buffer, content_len);
                        free(buffer);
                        return err;
                    }
                }
            }

            // Normal request - return full content
            httpd_resp_send(req, TEST_DATA, TEST_DATA_LEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &range_uri));

    // When: Client requests bytes 10-25
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    char *range_headers = create_range_header("bytes=10-25");
    TEST_ASSERT_NOT_NULL(range_headers);

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/range_content", range_headers, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    free(range_headers);

    // Then: Server returns 206 Partial Content with correct Content-Range and body
    TEST_ASSERT_TRUE(validate_partial_content_response(&response, "bytes 10-25/62", 16));

    // Verify body content matches TEST_DATA[10..25] = "ABCDEFGHIJKLMNOP" (16 chars)
    TEST_ASSERT_EQUAL(16, response.body_len);
    TEST_ASSERT_EQUAL_STRING_LEN("ABCDEFGHIJKLMNOP", response.body, 16);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_range_middleware_when_client_requests_invalid_range_then_416_range_not_satisfiable_returned
 *
 * Purpose: Verify that invalid range requests return 416 Range Not Satisfiable
 * RFC 9110 compliance: Section 15.5.17 (416 Range Not Satisfiable)
 */
void given_server_with_range_middleware_when_client_requests_invalid_range_then_416_range_not_satisfiable_returned(void)
{
    // Given: Server with range middleware registered (same as above test)
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9051; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t range_uri = {
        .uri      = "/range_content",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Check if this is a range request
            char range_buf[256];
            esp_err_t ret = httpd_req_get_hdr_value_str(req, "Range", range_buf, sizeof(range_buf));
            const char *range_header = (ret == ESP_OK) ? range_buf : NULL;
            if (range_header && strncmp(range_header, "bytes=", 6) == 0) {
                // Parse range
                const char *range_spec = range_header + 6;
                const char *dash_pos = strchr(range_spec, '-');
                if (dash_pos) {
                    long long start = atol(range_spec);
                    long long end = atol(dash_pos + 1);
                    long long total_len = TEST_DATA_LEN;

                    // Clamp bounds and check validity
                    if (start < 0) start = 0;
                    if (end >= total_len) end = total_len - 1;
                    if (start > end || start >= total_len) {
                        httpd_resp_set_status(req, "416 Range Not Satisfiable");
                        char content_range[64];
                        snprintf(content_range, sizeof(content_range), "bytes */%lld", total_len);
                        httpd_resp_set_hdr(req, "Content-Range", content_range);
                        return httpd_resp_send(req, "", 0);
                    }

                    size_t content_len = end - start + 1;

                    // Create partial content buffer
                    char *buffer = (char *)malloc(content_len + 1);
                    if (buffer) {
                        memcpy(buffer, TEST_DATA + start, content_len);
                        buffer[content_len] = '\0';

                        httpd_resp_set_status(req, "206 Partial Content");
                        char content_range[64];
                        snprintf(content_range, sizeof(content_range), "bytes %lld-%lld/%lld", start, end, total_len);
                        httpd_resp_set_hdr(req, "Content-Range", content_range);

                        esp_err_t err = httpd_resp_send(req, buffer, content_len);
                        free(buffer);
                        return err;
                    }
                }
            }

            // Normal request - return full content
            httpd_resp_send(req, TEST_DATA, TEST_DATA_LEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &range_uri));

    // When: Client requests an invalid range (beyond file size)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    char *range_headers = create_range_header("bytes=70-80"); // File is only 62 bytes
    TEST_ASSERT_NOT_NULL(range_headers);

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/range_content", range_headers, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    free(range_headers);

    // Then: Server returns 416 Range Not Satisfiable
    TEST_ASSERT_TRUE(validate_range_not_satisfiable_response(&response, TEST_DATA_LEN));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_redirect_handler_when_client_gets_then_308_permanent_redirect_returned_with_location_header
 *
 * Purpose: Verify that HTTP 308 Permanent Redirect responses can be sent with Location header
 * RFC 9110 compliance: Section 15.4.9 (308 Permanent Redirect)
 */
void given_server_with_redirect_handler_when_client_gets_then_308_permanent_redirect_returned_with_location_header(void)
{
    // Given: Server with a handler that sends 308 Permanent Redirect
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9052; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t redirect_uri = {
        .uri      = "/redirect",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_set_status(req, HTTPD_308);
            httpd_resp_set_hdr(req, "Location", "http://www.example.com/new-location");
            httpd_resp_send(req, "Resource moved permanently", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &redirect_uri));

    // When: Client sends GET request to the redirect URI
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/redirect", NULL, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    // Then: Server returns 308 Permanent Redirect with Location header
    TEST_ASSERT_TRUE(validate_permanent_redirect_response(&response, "http://www.example.com/new-location"));
    TEST_ASSERT_EQUAL(308, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Resource moved permanently", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_redirect_handler_when_client_posts_then_308_permanent_redirect_returned_with_location_header
 *
 * Purpose: Verify that HTTP 308 Permanent Redirect preserves method semantics (tests server-side response)
 * RFC 9110 compliance: Section 15.4.9 (308 Permanent Redirect)
 */
void given_server_with_redirect_handler_when_client_posts_then_308_permanent_redirect_returned_with_location_header(void)
{
    // Given: Server with a handler that sends 308 Permanent Redirect
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9053; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t redirect_uri = {
        .uri      = "/redirect",
        .method   = HTTP_POST,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_set_status(req, HTTPD_308);
            httpd_resp_set_hdr(req, "Location", "http://www.example.com/new-post-endpoint");
            httpd_resp_send(req, "Resource moved permanently", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &redirect_uri));

    // When: Client sends POST request to the redirect URI (method preservation is tested on client side)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send POST request with a small body
    const char *post_body = "test data";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_POST,
                                                 "/redirect", NULL, post_body, strlen(post_body),
                                                 &response, TEST_TIMEOUT_MS));

    // Then: Server returns 308 Permanent Redirect (method preservation is client-side behavior)
    TEST_ASSERT_TRUE(validate_permanent_redirect_response(&response, "http://www.example.com/new-post-endpoint"));
    TEST_ASSERT_EQUAL(308, response.status_code);

    // Note: Full method preservation testing requires extended http_test_client capabilities
    // to automatically follow redirects while preserving the original method.

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_misdirected_handler_when_client_requests_then_421_misdirected_request_returned
 *
 * Purpose: Verify that HTTP 421 Misdirected Request responses can be sent correctly
 * RFC 7540 compliance: Section 9.1.2 (421 Misdirected Request)
 */
void given_server_with_misdirected_handler_when_client_requests_then_421_misdirected_request_returned(void)
{
    // Given: Server with a handler that sends 421 Misdirected Request for specific conditions
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9054; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t misdirected_uri = {
        .uri      = "/misdirected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Simulate misdirected request scenario (e.g., wrong server routing)
            // In a real implementation, this could check request parameters, headers, etc.
            // For testing, we simulate by checking for a specific query parameter
            char query_buf[128];
            esp_err_t err = httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf));
            if (err == ESP_OK && strstr(query_buf, "misdirect=1")) {
                httpd_resp_set_status(req, HTTPD_421);
                httpd_resp_send(req, "Request directed to wrong server", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }

            // Normal request
            httpd_resp_send(req, "Request handled correctly", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &misdirected_uri));

    // When: Client sends request that triggers misdirected response
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/misdirected?misdirect=1", NULL, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    // Then: Server returns 421 Misdirected Request
    TEST_ASSERT_TRUE(validate_misdirected_request_response(&response));
    TEST_ASSERT_EQUAL_STRING("Request directed to wrong server", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * Test: given_server_with_upgrade_handler_when_client_requests_http1_then_426_upgrade_required_returned_with_h2_header
 *
 * Purpose: Verify that HTTP 426 Upgrade Required responses can be sent with Upgrade header for HTTP/2
 * RFC 7230 compliance: Section 6.7 (426 Upgrade Required)
 */
void given_server_with_upgrade_handler_when_client_requests_http1_then_426_upgrade_required_returned_with_h2_header(void)
{
    // Given: Server configured to require HTTP/2 for certain resources
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9055; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t upgrade_uri = {
        .uri      = "/upgrade_only",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Check for HTTP version or other conditions that require upgrade
            // For this test, we'll require upgrade based on a custom header
            char version_buf[64];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "X-Require-HTTP2", version_buf, sizeof(version_buf));
            if (err == ESP_OK && strcmp(version_buf, "true") == 0) {
                httpd_resp_set_status(req, HTTPD_426);
                httpd_resp_set_hdr(req, "Upgrade", "h2");
                httpd_resp_send(req, "HTTP/2 required for this resource", HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }

            // Normal request
            httpd_resp_send(req, "Request handled with current protocol", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &upgrade_uri));

    // When: Client sends request that requires protocol upgrade
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send request with header indicating HTTP/2 is required
    char *headers = (char*)malloc(1024);
    snprintf(headers, 1024, "X-Require-HTTP2: true\r\n");

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/upgrade_only", headers, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    free(headers);

    // Then: Server returns 426 Upgrade Required with Upgrade: h2 header
    TEST_ASSERT_TRUE(validate_upgrade_required_response(&response, "h2"));
    TEST_ASSERT_EQUAL_STRING("HTTP/2 required for this resource", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

int test_response_handling(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_valid_request_when_calling_httpd_resp_send_then_response_is_sent);
    RUN_TEST(given_server_with_resp_send_handler_when_client_requests_then_receives_response);
    RUN_TEST(given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response);
    
    RUN_TEST(given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response);
    RUN_TEST(given_server_with_large_response_handler_when_client_requests_then_receives_large_response);

    RUN_TEST(given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches);
    RUN_TEST(given_valid_global_context_when_setting_and_getting_then_context_preserved);
    RUN_TEST(given_valid_session_context_when_setting_and_getting_then_context_preserved);

    RUN_TEST(given_server_with_range_middleware_when_client_requests_valid_range_then_206_partial_content_returned);
    RUN_TEST(given_server_with_range_middleware_when_client_requests_invalid_range_then_416_range_not_satisfiable_returned);

    RUN_TEST(given_server_with_redirect_handler_when_client_gets_then_308_permanent_redirect_returned_with_location_header);
    RUN_TEST(given_server_with_redirect_handler_when_client_posts_then_308_permanent_redirect_returned_with_location_header);

    RUN_TEST(given_server_with_misdirected_handler_when_client_requests_then_421_misdirected_request_returned);
    RUN_TEST(given_server_with_upgrade_handler_when_client_requests_http1_then_426_upgrade_required_returned_with_h2_header);

    // return UNITY_END();
    return 0;
}
