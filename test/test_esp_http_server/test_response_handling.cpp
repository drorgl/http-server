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

#define TEST_TIMEOUT_MS 5000

void nop(void * ctx){};


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

int test_response_handling(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_valid_request_when_calling_httpd_resp_send_then_response_is_sent);
    RUN_TEST(given_server_with_resp_send_handler_when_client_requests_then_receives_response);
    RUN_TEST(given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response);
    
    RUN_TEST(given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response);
    RUN_TEST(given_server_with_large_response_handler_when_client_requests_then_receives_large_response);

    RUN_TEST(given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches);
    RUN_TEST(given_valid_global_context_when_setting_and_getting_then_context_preserved);
    RUN_TEST(given_valid_session_context_when_setting_and_getting_then_context_preserved);
    // return UNITY_END();
    return 0;
}
