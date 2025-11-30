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

#define TAG "TEST_HTTPD_UTILITIES"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define TEST_BUFFER_SIZE 1024


/**
 * Test: given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values
 *
 * Purpose: Verify that httpd_req_get_hdr_value_str correctly extracts values from multiple headers in a real request.
 * Expected: The function returns ESP_OK and the buffer contains the correct header values.
 */
void given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values(void)
{
    // Given: A running server with a GET handler that reads custom headers
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9012; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t get_uri = {
        .uri      = "/test_headers",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char header_val1[32] = {0};
            char header_val2[32] = {0};
            char header_val3[32] = {0};
            esp_err_t err1, err2, err3;

            err1 = httpd_req_get_hdr_value_str(req, "X-Custom-Header-1", header_val1, sizeof(header_val1));
            err2 = httpd_req_get_hdr_value_str(req, "X-Custom-Header-2", header_val2, sizeof(header_val2));
            err3 = httpd_req_get_hdr_value_str(req, "X-Non-Existent-Header", header_val3, sizeof(header_val3));

            if (err1 == ESP_OK && strcmp(header_val1, "Value1") == 0 &&
                err2 == ESP_OK && strcmp(header_val2, "Value2") == 0 &&
                err3 == ESP_ERR_NOT_FOUND) {
                httpd_resp_send(req, "Headers OK", HTTPD_RESP_USE_STRLEN);
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Headers NOT OK. H1:%s (%d), H2:%s (%d), H3:%s (%d)",
                         header_val1, err1, header_val2, err2, header_val3, err3);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
            }
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &get_uri));

    // When: A client connects and sends a request with multiple custom headers
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers_str = "X-Custom-Header-1: Value1\r\nX-Custom-Header-2: Value2\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/test_headers", headers_str, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with "Headers OK"
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Headers OK", response.body);
    http_test_client_free_response(&response); // Free response body and headers

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}


/**
 * Test: given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value
 *
 * Purpose: Verify that http_test_client_get_header correctly extracts the value of the last header
 *          even if it's not terminated by \r\n.
 * Expected: The function returns the correct header value.
 */
void given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value(void)
{
    // Given: A mock http_test_response_t with headers where the last one has no trailing \r\n
    http_test_response_t response = {0};
    const char *test_headers_str = "Content-Type: application/json\r\nContent-Length: 39\r\nX-Custom-Header: CustomValue";
    response.headers = strdup(test_headers_str);
    TEST_ASSERT_NOT_NULL(response.headers);

    // When: Calling http_test_client_get_header for the last header
    const char *header_value = http_test_client_get_header(&response, "X-Custom-Header");

    // Then: The correct value is returned
    TEST_ASSERT_NOT_NULL(header_value);
    TEST_ASSERT_EQUAL_STRING("CustomValue", header_value);

    // Cleanup
    free((void*)header_value); // Free the allocated string by the function
    http_test_client_free_response(&response); // Free response headers
}


// Mock recv function for httpd_req_recv testing
static int mock_recv_data(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags) {
    (void)hd; (void)sockfd; (void)flags; // Unused parameters
    const char *test_data = "Hello, world!";
    size_t test_data_len = strlen(test_data);

    if (buf_len > test_data_len) {
        buf_len = test_data_len;
    }
    memcpy(buf, test_data, buf_len);
    return buf_len;
}



void given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data(void)
{
    // Given: Started HTTP server and a mock request with content
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8097; // Use a different port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    struct httpd_req_mutable_uri {
        httpd_handle_t  handle;
        int             method;
        char            uri_buffer[CONFIG_HTTPD_MAX_URI_LEN + 1];
        size_t          content_len;
        void           *aux;
        void           *user_ctx;
        void           *sess_ctx;
        httpd_free_ctx_fn_t free_ctx;
        bool ignore_sess_ctx_changes;
    } mock_req_data;

    memset(&mock_req_data, 0, sizeof(mock_req_data));
    httpd_req_t *mock_req = (httpd_req_t*)&mock_req_data;
    mock_req->handle = handle;
    mock_req->content_len = strlen("Hello, world!"); // Simulate content length

    struct httpd_req_aux aux = {0};
    mock_req->aux = &aux;
    aux.remaining_len = mock_req->content_len; /* Initialize remaining_len */

    // Create a mock sock_db and assign it to aux.sd
    struct sock_db mock_sd = {0};
    mock_sd.fd = 101; // Dummy socket FD
    mock_sd.handle = handle;
    mock_sd.recv_fn = mock_recv_data; // Set the mock recv function
    aux.sd = &mock_sd; // Assign the mock sock_db to aux.sd

    // mock_req->free_ctx
    snprintf(mock_req_data.uri_buffer, sizeof(mock_req_data.uri_buffer), "/test_recv");

    int mock_sockfd = 101; // Dummy socket FD (re-declared for cleanup)
    // No need to call httpd_sess_set_recv_override as we directly set the recv_fn in mock_sd
    // httpd_sess_set_recv_override(handle, mock_sockfd, NULL);

    char recv_buf[TEST_BUFFER_SIZE];
    memset(recv_buf, 0, sizeof(recv_buf));

    // When: Calling httpd_req_recv
    int bytes_received = httpd_req_recv(mock_req, recv_buf, sizeof(recv_buf));

    // Then: Data is received correctly
    TEST_ASSERT_EQUAL(strlen("Hello, world!"), bytes_received);
    TEST_ASSERT_EQUAL_STRING("Hello, world!", recv_buf);

    // Test NULL request
    bytes_received = httpd_req_recv(NULL, recv_buf, sizeof(recv_buf));
    TEST_ASSERT_EQUAL(HTTPD_SOCK_ERR_INVALID, bytes_received);

    // Test NULL buffer
    bytes_received = httpd_req_recv(mock_req, NULL, sizeof(recv_buf));
    TEST_ASSERT_EQUAL(HTTPD_SOCK_ERR_INVALID, bytes_received);

    // Cleanup
    httpd_sess_set_recv_override(handle, mock_sockfd, NULL); // Clear override
    httpd_stop(handle);
}



// Mock send function for httpd_send testing
static int mock_send_data(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags) {
    (void)hd; (void)sockfd; (void)flags; // Unused parameters
    // In a real test, you might store the sent data to verify it later
    // For now, we just return the length to simulate a successful send
    return buf_len;
}


/**
 * Test: given_valid_request_when_calling_httpd_send_then_sends_data
 *
 * Purpose: Verify that httpd_send() correctly sends data.
 * Expected: The function returns the number of bytes sent, simulating a successful transmission.
 */
void given_valid_request_when_calling_httpd_send_then_sends_data(void)
{
    // Given: Started HTTP server and a mock request
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8098; // Use a different port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    struct httpd_req_mutable_uri {
        httpd_handle_t  handle;
        int             method;
        char            uri_buffer[CONFIG_HTTPD_MAX_URI_LEN + 1];
        size_t          content_len;
        void           *aux;
        void           *user_ctx;
        void           *sess_ctx;
        httpd_free_ctx_fn_t free_ctx;
        bool ignore_sess_ctx_changes;
    } mock_req_data;

    memset(&mock_req_data, 0, sizeof(mock_req_data));
    httpd_req_t *mock_req = (httpd_req_t*)&mock_req_data;
    mock_req->handle = handle;
    struct sock_db mock_sock_db = {
        .fd = 102, // Dummy socket FD
        .ctx = NULL,
        .ignore_sess_ctx_changes = false,
        .transport_ctx = NULL,
        .handle = handle,
        .free_ctx = NULL,
        .free_transport_ctx = NULL,
        .send_fn = mock_send_data,
        .recv_fn = NULL,
        .pending_fn = NULL,
        .lru_counter = 0,
        .lru_socket = false,
        .pending_data = {0}, // Initialize array to zeros
        .pending_len = 0,
        .for_async_req = false,
    };

    struct httpd_req_aux aux = {
        .sd = &mock_sock_db,
    };
    mock_req->aux = &aux;
    snprintf(mock_req_data.uri_buffer, sizeof(mock_req_data.uri_buffer), "/test_send");

    const char *data_to_send = "Response data";
    size_t data_len = strlen(data_to_send);

    // When: Calling httpd_send
    int bytes_sent = httpd_send(mock_req, data_to_send, data_len);

    // Then: Data is sent successfully
    TEST_ASSERT_EQUAL(data_len, bytes_sent);

    // Test NULL request
    bytes_sent = httpd_send(NULL, data_to_send, data_len);
    TEST_ASSERT_EQUAL(HTTPD_SOCK_ERR_INVALID, bytes_sent);

    // Test NULL buffer
    bytes_sent = httpd_send(mock_req, NULL, data_len);
    TEST_ASSERT_EQUAL(HTTPD_SOCK_ERR_INVALID, bytes_sent);

    // Cleanup
    httpd_stop(handle);
}



// Custom URI match function: matches URIs starting with "/custom/"
static bool custom_uri_match_fn(const char *uri, const char *uri_to_match, size_t uri_len) {
    // uri_to_match is the registered URI (e.g., "/custom/test")
    // uri is the incoming request URI (e.g., "/custom/test?param=value")

    // Check if the incoming URI starts with "/custom/"
    if (strncmp(uri, "/custom/", strlen("/custom/")) == 0) {
        // If it starts with "/custom/", then perform a standard match against the registered URI
        // This example assumes a direct match for simplicity, but could be more complex
        return httpd_uri_match_wildcard(uri_to_match, uri, uri_len);
    }
    return false;
}


// A flag to be set by the handler
static bool custom_match_handler_invoked = false;


// Handler for the custom matched URI
static esp_err_t custom_match_test_handler(httpd_req_t *req)
{
    custom_match_handler_invoked = true;
    httpd_resp_send(req, "Custom Match Handler Invoked", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * Test: given_server_with_custom_uri_match_fn_when_request_matches_then_handler_invoked
 *
 * Purpose: Verify that the custom `uri_match_fn` callback in the server config correctly
 *          matches URIs and invokes the associated handler.
 * Expected: The custom handler is invoked for matching URIs, and 404 for non-matching.
 */
void given_server_with_custom_uri_match_fn_when_request_matches_then_handler_invoked(void)
{
    // Given: A running server with a custom uri_match_fn and a registered handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9009; // Use a unique port
    config.uri_match_fn = custom_uri_match_fn; // Set custom match function
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t custom_uri = {
        .uri      = "/custom/test",
        .method   = HTTP_GET,
        .handler  = custom_match_test_handler,
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &custom_uri));

    // Initialize http_test_client
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Test 1 (Match): Client sends a request that should match the custom URI
    custom_match_handler_invoked = false;
    http_test_response_t response1 = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/custom/test", NULL, NULL, 0, &response1, TEST_TIMEOUT_MS));

    // Then: The custom handler was invoked and the correct response is received
    TEST_ASSERT_TRUE(custom_match_handler_invoked);
    TEST_ASSERT_EQUAL(200, response1.status_code);
    TEST_ASSERT_NOT_NULL(response1.body);
    TEST_ASSERT_EQUAL_STRING("Custom Match Handler Invoked", response1.body);
    http_test_client_free_response(&response1);

    // Test 2 (No Match): Client sends a request that should NOT match the custom URI
    custom_match_handler_invoked = false;
    http_test_response_t response2 = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/other/path", NULL, NULL, 0, &response2, TEST_TIMEOUT_MS));

    // Then: The custom handler was NOT invoked, and a 404 Not Found is returned
    TEST_ASSERT_FALSE(custom_match_handler_invoked);
    TEST_ASSERT_EQUAL(404, response2.status_code);
    http_test_client_free_response(&response2);

    // Cleanup
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

// A flag to be set by the handler
static bool test_handler_invoked = false;

// Simple handler for testing
static esp_err_t test_get_handler(httpd_req_t *req)
{
    test_handler_invoked = true;
    const char* resp_str = "Test Response";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}



/**
 * Test: given_server_with_uri_handler_when_client_connects_then_handler_is_invoked
 *
 * Purpose: Verify that a network request to a registered URI correctly invokes the associated handler.
 * Expected: The handler is invoked, and the client receives the correct HTTP response.
 */
void given_server_with_uri_handler_when_client_connects_then_handler_is_invoked(void)
{
    // Given: A running server with a registered handler
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9001;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t get_uri = {
        .uri      = "/test",
        .method   = HTTP_GET,
        .handler  = test_get_handler,
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &get_uri));

    test_handler_invoked = false;

    // When: A client connects and sends a request using http_test_client
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The handler was invoked and the correct response is received
    TEST_ASSERT_TRUE(test_handler_invoked);
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Test Response", response.body);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}


void dummy(void)
{
    // This is a placeholder function to maintain the original structure
    // All actual tests are now in the specific test functions above
}


int test_utilities(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values);
    RUN_TEST(given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value);
    RUN_TEST(given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data);
    RUN_TEST(given_valid_request_when_calling_httpd_send_then_sends_data);
    RUN_TEST(given_server_with_custom_uri_match_fn_when_request_matches_then_handler_invoked);
    RUN_TEST(given_server_with_uri_handler_when_client_connects_then_handler_is_invoked);
    RUN_TEST(dummy);
    // return UNITY_END();
    return 0;
}
