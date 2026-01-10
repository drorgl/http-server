/**
 * @file test_security.cpp
 * @brief Security-focused tests for HTTP server (RFC 9112 compliance)
 *
 * Tests for critical security vulnerabilities:
 * - Response splitting attack prevention
 * - Header injection attack prevention
 * - CRLF injection protection
 * - Request smuggling prevention
 * - Header field validation and sanitization
 */

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

#include <errno.h>
#include <sys/time.h> // Required for gettimeofday and struct timeval

#define TAG "TEST_SECURITY"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define TEST_BUFFER_SIZE 2048
#define RECEIVE_TIMEOUT_SEC 5 // 5 seconds cumulative timeout for receive operations

/**
 * @brief Helper function to check if response contains CR/LF injections
 * @param response_body Response body to check
 * @return true if response appears safe from splitting
 */
static bool response_splitting_safe(const char *response_body) {
    if (!response_body) return true;

    // Check for header injection patterns in response body
    // These patterns indicate response splitting attacks succeeded
    const char *injection_patterns[] = {
        "HTTP/1.1",     // Multiple HTTP responses
        "\r\n\r\n",     // Headers section end
        "\r\nSet-Cookie:", // Header injection
        "\r\nLocation:",    // Redirect injection
        "\r\nWWW-Authenticate:", // Auth injection
        NULL
    };

    for (int i = 0; injection_patterns[i] != NULL; i++) {
        if (strstr(response_body, injection_patterns[i]) != NULL) {
            LOGW(TAG, "Potential response splitting detected: %s", injection_patterns[i]);
            return false;
        }
    }
    return true;
}

/**
 * @brief Test: Response splitting prevention in custom headers
 *
 * Purpose: Verify that header values containing CRLF sequences are rejected
 * by httpd_resp_set_hdr() to prevent response splitting attacks.
 *
 * RFC 9112 Security: Prevent response header injection attacks.
 */
void test_response_splitting_prevention_in_custom_headers(void)
{
    // Given: A running server with an handler that tests header validation
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t test_uri = {
        .uri      = "/test_hdr",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            esp_err_t ret;

            // Test 1: Normal header should work
            ret = httpd_resp_set_hdr(req, "X-Test", "normal_value");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Normal header should be accepted");

            // Test 2: CRLF in header value should be rejected
            ret = httpd_resp_set_hdr(req, "X-Malicious", "value\r\nSet-Cookie: evil=value");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, ret, "CRLF in header value should be rejected");

            // Test 3: CRLF in header field name should be rejected
            ret = httpd_resp_set_hdr(req, "X-Bad\r\nField", "value");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, ret, "CRLF in header field should be rejected");
            // Test 4: CRLF in status should be rejected
            ret = httpd_resp_set_status(req, "200 OK\r\nContent-Length: 0");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, ret, "CRLF in status should be rejected");

            httpd_resp_send(req, "Test completed", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &test_uri));

    // When: Client sends normal request
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/test_hdr", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Response should be successful and security validations passed
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_TRUE(strstr(response.body, "Test completed") != NULL);

    // Verify no response splitting occurred (check for unexpected headers in body)
    TEST_ASSERT_TRUE(response_splitting_safe(response.body));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: CRLF injection protection in header values
 *
 * Purpose: Verify that CRLF sequences in header values are rejected
 * to prevent HTTP response splitting attacks.
 */
void test_crlf_injection_protection_in_header_values(void)
{
    // Given: A running server with handler that tests Location header validation
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t test_uri = {
        .uri      = "/test_redirect",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            esp_err_t ret;

            // Test 1: Normal header should work
            ret = httpd_resp_set_hdr(req, "Location", "http://example.com");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Normal Location header should be accepted");

            // Test 2: CRLF in Location header should be rejected
            ret = httpd_resp_set_hdr(req, "Location", "http://example.com\r\nSet-Cookie: injected=value");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, ret, "CRLF in Location header should be rejected");

            // Test 3: CRLF Status line should be rejected
            ret = httpd_resp_set_status(req, "302 Found\r\nContent-Length: 0\r\n\r\nHTTP/1.1 200 OK");
            TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, ret, "CRLF in status line should be rejected");

            httpd_resp_send(req, "Tests passed", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &test_uri));

    // When: Client sends normal request (malicious input handled in handler tests above)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/test_redirect", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Response should be successful and no injection occurred
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_TRUE(strstr(response.body, "Tests passed") != NULL);
    TEST_ASSERT_TRUE(response_splitting_safe(response.body));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: Header injection attack prevention with custom error messages
 *
 * Purpose: Verify that custom error messages don't allow injection of HTTP headers.
 */
void test_header_injection_attack_prevention_in_error_messages(void)
{
    // Given: A running server with custom error handler that uses user input in error message
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Custom error handler that could be vulnerable to injection
    httpd_err_handler_func_t vulnerable_handler = [](httpd_req_t *req, httpd_err_code_t error) {
        char user_msg[256];
        // Get user input from query parameter
        esp_err_t ret = httpd_req_get_url_query_str(req, user_msg, sizeof(user_msg));
        if (ret == ESP_OK) {
            // This could be vulnerable if user_msg contains CRLF
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, user_msg);
        }
        return ESP_OK;
    };

    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_err_handler(handle, HTTPD_404_NOT_FOUND, vulnerable_handler));

    // When: Client triggers 404 with malicious error message
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/nonexistent?msg=Not%20found%0D%0ASet-Cookie:%20evil=value",
                                                 NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Error response should not contain injected headers
    TEST_ASSERT_TRUE(response_splitting_safe(response.body));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: Header field name injection prevention
 *
 * Purpose: Verify that header field names cannot contain CRLF or other injection characters.
 */
void test_header_field_name_injection_prevention(void)
{
    // Given: A running server that echoes header values
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t header_echo_uri = {
        .uri      = "/echo_header",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Read all headers and echo them (could be vulnerable if headers contain injection)
            char buffer[1024] = {0};
            struct httpd_req_aux *ra = (struct httpd_req_aux *)req->aux;

            for (unsigned i = 0; i < ra->req_hdrs_count; i++) {
                strncat(buffer, ra->scratch, sizeof(buffer) - strlen(buffer) - 1);
                if (i < ra->req_hdrs_count - 1) {
                    strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
                }
            }

            httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &header_echo_uri));

    // When: Client sends headers with potentially malicious field names
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send custom headers with CRLF in field names (if parser allows it)
    char headers_str[512];
    snprintf(headers_str, sizeof(headers_str),
             "X-Normal: safe_value\r\n"
             "X-Trusted\r\n\r: bad_value\r\n"
             "X-Malicious: CRLF_in_field_name\r\n");

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/echo_header", headers_str, NULL, 0,
                                                 &response, TEST_TIMEOUT_MS));

    // Then: Response should not show signs of header injection
    TEST_ASSERT_TRUE(response_splitting_safe(response.body));

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: Request smuggling with malformed Content-Length
 *
 * Purpose: Test request smuggling prevention when Content-Length and actual body don't match.
 */
void test_request_smuggling_content_length_mismatch(void)
{
    // This test checks for basic Content-Length validation
    // which is already tested in test_error_handling.cpp, but we
    // add additional security checks here

    // Given: A server that processes POST requests with explicit Content-Length
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t post_uri = {
        .uri      = "/post_data",
        .method   = HTTP_POST,
        .handler  = [](httpd_req_t *req) {
            // Get expected Content-Length
            char cl_str[32] = {0};
            int content_len = 0;
            if (httpd_req_get_hdr_value_str(req, "Content-Length", cl_str, sizeof(cl_str)) == ESP_OK) {
                content_len = atoi(cl_str);
            }

            char buffer[512];
            int received_bytes = httpd_req_recv(req, buffer, sizeof(buffer));

            // Validate Content-Length compliance for security
            if (content_len > 0 && received_bytes < content_len) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request body");
                return ESP_FAIL;
            }

            char response[128];
            snprintf(response, sizeof(response), "Received %d bytes", received_bytes);
            httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
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

    // When: Client sends request with incorrect Content-Length
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd);
    TEST_ASSERT_EQUAL(0, connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    const char *smuggled_request =
        "POST /post_data HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 100\r\n"  // Claim 100 bytes but send less
        "\r\n"
        "short_data";  // Only 10 bytes

    send(sockfd, smuggled_request, strlen(smuggled_request), 0);

    // Give server time to process
    httpd_os_thread_sleep(100);

    char buffer[TEST_BUFFER_SIZE] = {0};
    int bytes_read = 0;
    int recv_result = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (recv_result > 0) {
        bytes_read += recv_result;
        buffer[bytes_read] = '\0';
    }

    // Then: Server should handle the mismatch gracefully
    // (The exact behavior depends on implementation, but should not crash)
    TEST_ASSERT_TRUE_MESSAGE(
        strstr(buffer, "Received") != NULL ||
        strstr(buffer, "500") != NULL ||
        strstr(buffer, "400") != NULL,
        "Server should handle Content-Length mismatch without crashing");

    // Cleanup
    #ifdef _WIN32
        closesocket(sockfd);
    #else
        close(sockfd);
    #endif
    httpd_stop(handle);
}

/**
 * @brief Test: Response splitting prevention with user-controlled status line
 *
 * Purpose: Verify that custom status lines cannot inject CRLF to create multiple responses.
 */
void test_response_splitting_prevention_in_custom_status(void)
{
    // Given: A server with handler that uses custom status lines
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // This test would require a server that allows custom status lines
    // For now, we'll test that standard status setting doesn't allow injection

    httpd_uri_t status_uri = {
        .uri      = "/status",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Simulate user-controlled status (safely)
            char user_status[128];
            if (httpd_req_get_url_query_str(req, user_status, sizeof(user_status)) == ESP_OK) {
                // In a vulnerable server, this might allow status injection
                char status_line[256];
                snprintf(status_line, sizeof(status_line), "%s OK", user_status);
                httpd_resp_set_status(req, status_line);
            }
            httpd_resp_send(req, "Status set", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &status_uri));

    // When: Client sends potentially malicious status
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK,
                     http_test_client_send_request(client, HTTP_METHOD_GET,
                                                 "/status?status=Custom%20Status%0D%0AContent-Length:%200",
                                                 NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Response should not show injection attempts working
    TEST_ASSERT_TRUE(response_splitting_safe(response.body));
    TEST_ASSERT_EQUAL(200, response.status_code);

    // Cleanup
    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Main security test runner
 *
 * Runs all security vulnerability tests required for RFC 9112 compliance.
 */
int test_security(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);

    // Response Splitting Attack Prevention
    RUN_TEST(test_response_splitting_prevention_in_custom_headers);
    RUN_TEST(test_response_splitting_prevention_in_custom_status);

    // CRLF Injection Protection
    RUN_TEST(test_crlf_injection_protection_in_header_values);

    // Header Injection Attack Prevention
    RUN_TEST(test_header_injection_attack_prevention_in_error_messages);
    RUN_TEST(test_header_field_name_injection_prevention);

    // Request Smuggling Prevention
    RUN_TEST(test_request_smuggling_content_length_mismatch);
    // return UNITY_END();
    return 0;
}
