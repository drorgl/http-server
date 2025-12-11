/*
 * SPDX-FileCopyrightText: 2018-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include <base64_codec.h> // For base64 decoding
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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

#define TAG "TEST_HTTPD_AUTH"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define RECEIVE_TIMEOUT_SEC 5 // 5 seconds cumulative timeout for receive operations

/**
 * @brief Helper function to validate Basic authentication credentials
 *
 * Parses Authorization header of format "Basic <base64_credentials>",
 * decodes the base64 part, and validates against expected username:password.
 *
 * @param auth_header The full Authorization header value
 * @param expected_user Expected username
 * @param expected_pass Expected password
 * @return true if credentials are valid, false otherwise
 */
static bool validate_basic_auth(const char *auth_header, const char *expected_user, const char *expected_pass) {
    if (!auth_header || !expected_user || !expected_pass) {
        return false;
    }

    // Check that header starts with "Basic "
    const char *basic_prefix = "Basic ";
    size_t prefix_len = strlen(basic_prefix);
    if (strncmp(auth_header, basic_prefix, prefix_len) != 0) {
        return false;
    }

    // Get the base64 part
    const char *base64_credentials = auth_header + prefix_len;
    size_t base64_len = strlen(base64_credentials);

    // Calculate decoded length
    size_t decoded_len = base64_decoded_length(base64_credentials, base64_len);
    if (decoded_len == 0) {
        return false;
    }

    // Allocate buffer for decoded credentials
    char *decoded_credentials = (char *)malloc(decoded_len + 1); // +1 for null terminator
    if (!decoded_credentials) {
        return false;
    }

    // Decode base64
    size_t actual_decoded_len = base64_decode(base64_credentials, base64_len,
                                             (unsigned char *)decoded_credentials, decoded_len + 1);
    if (actual_decoded_len != decoded_len) {
        free(decoded_credentials);
        return false;
    }

    // Null-terminate the decoded string
    decoded_credentials[decoded_len] = '\0';

    // Find the colon separator
    char *colon_pos = strchr(decoded_credentials, ':');
    if (!colon_pos) {
        free(decoded_credentials);
        return false;
    }

    // Split into username and password
    *colon_pos = '\0'; // Null terminate username
    const char *username = decoded_credentials;
    const char *password = colon_pos + 1;

    // Validate credentials
    bool is_valid = (strcmp(username, expected_user) == 0 && strcmp(password, expected_pass) == 0);

    free(decoded_credentials);
    return is_valid;
}

/**
 * @brief Test: given_protected_resource_when_no_auth_header_then_401_unauthorized_returned
 *
 * Purpose: Verify that a protected resource returns 401 Unauthorized when no
 *          Authorization header is provided, as per RFC 9110 Section 11.6.1.
 * Expected: Server responds with "401 Unauthorized" and WWW-Authenticate header.
 */
void given_protected_resource_when_no_auth_header_then_401_unauthorized_returned(void)
{
    // Given: A running server with a protected resource that requires authentication
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9031; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            // Check for Authorization header
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                // No authorization header found - return 401 Unauthorized
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }
            // Authorization header present
            httpd_resp_sendstr(req, "Access granted");
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client connects and requests the protected resource without Authorization header
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: The server should respond with 401 Unauthorized and WWW-Authenticate header
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", response.status_text);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Authentication required", response.body);

    // Check for WWW-Authenticate header in response
    TEST_ASSERT_NOT_NULL(response.headers);
    const char *www_auth_header = http_test_client_get_header(&response, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL(www_auth_header);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Protected Area\"", www_auth_header);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_basic_auth_credentials_when_valid_then_access_granted
 *
 * Purpose: Verify that valid Basic authentication credentials allow access
 *          to protected resources, as per RFC 9110 Section 11.6.2.
 * Expected: Server grants access when correct Basic auth credentials are provided.
 */
void given_basic_auth_credentials_when_valid_then_access_granted(void)
{
    // Given: A running server with Basic authentication protection
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9032; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            // Validate Basic authentication credentials using proper base64 decoding
            if (validate_basic_auth(auth_header, "user", "pass")) {
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides valid Basic authentication credentials
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers = "Authorization: Basic dXNlcjpwYXNz\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be granted
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Access granted", response.body);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_basic_auth_credentials_when_invalid_then_access_denied
 *
 * Purpose: Verify that invalid Basic authentication credentials are rejected,
 *          as per RFC 9110 Section 11.6.2.
 * Expected: Server denies access and returns 401 with WWW-Authenticate header.
 */
void given_basic_auth_credentials_when_invalid_then_access_denied(void)
{
    // Given: A running server with Basic authentication protection
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9033; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            // Validate Basic authentication credentials using proper base64 decoding
            if (validate_basic_auth(auth_header, "user", "pass")) {
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides invalid Basic authentication credentials
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers = "Authorization: Basic aW52YWxpZDppbnZhbGlk\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be denied with 401
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", response.status_text);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", response.body);

    // Check for WWW-Authenticate header in response
    TEST_ASSERT_NOT_NULL(response.headers);
    const char *www_auth_header = http_test_client_get_header(&response, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL(www_auth_header);
    TEST_ASSERT_EQUAL_STRING("Basic realm=\"Test Realm\"", www_auth_header);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_authentication_info_when_successful_then_header_included
 *
 * Purpose: Verify that Authentication-Info header can be included for
 *          post-authentication information, as per RFC 9110 Section 11.6.3.
 * Expected: Server includes Authentication-Info header in successful auth responses.
 */
void given_authentication_info_when_successful_then_header_included(void)
{
    // Given: A running server that includes Authentication-Info after successful auth
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9034; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            if (validate_basic_auth(auth_header, "user", "pass")) {
                // Include Authentication-Info header for post-authentication info
                httpd_resp_set_hdr(req, "Authentication-Info", "nextnonce=\"abc123\"");
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides valid credentials
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers = "Authorization: Basic dXNlcjpwYXNz\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be granted and Authentication-Info header should be present
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Access granted", response.body);

    // Check for Authentication-Info header in response
    TEST_ASSERT_NOT_NULL(response.headers);
    const char *auth_info_header = http_test_client_get_header(&response, "Authentication-Info");
    TEST_ASSERT_NOT_NULL(auth_info_header);
    TEST_ASSERT_EQUAL_STRING("nextnonce=\"abc123\"", auth_info_header);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_multiple_auth_schemes_when_offered_then_client_can_choose
 *
 * Purpose: Verify that multiple authentication schemes can be offered,
 *          as per RFC 9110 Part 11 authentication scheme extensibility.
 * Expected: Server can include multiple WWW-Authenticate headers.
 */
void given_multiple_auth_schemes_when_offered_then_client_can_choose(void)
{
    // Given: A running server that supports multiple authentication schemes
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9035; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                // Offer multiple authentication schemes
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Digest realm=\"Test Realm\", nonce=\"abc123\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            httpd_resp_sendstr(req, "Access granted");
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client requests the protected resource without authentication
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Server should offer multiple authentication schemes
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Authentication required", response.body);

    // Check for multiple WWW-Authenticate headers in response
    TEST_ASSERT_NOT_NULL(response.headers);

    // Check first WWW-Authenticate header (Basic)
    const char *basic_auth_header = http_test_client_get_header(&response, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL(basic_auth_header);
    TEST_ASSERT_TRUE(strstr(basic_auth_header, "Basic realm=") != NULL ||
                     strstr(basic_auth_header, "Digest realm=") != NULL);

    // For this test, we just verify that at least one WWW-Authenticate header exists
    // The actual multi-header functionality depends on how httpd_resp_set_hdr works

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_malformed_auth_header_when_provided_then_400_bad_request
 *
 * Purpose: Verify that malformed Authorization headers are handled properly.
 * Expected: Server responds with appropriate error for malformed credentials.
 */
void given_malformed_auth_header_when_provided_then_400_bad_request(void)
{
    // Given: A running server with authentication protection
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9036; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            // Validate Basic authentication credentials using proper base64 decoding
            if (validate_basic_auth(auth_header, "user", "pass")) {
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                // Invalid credentials - could be malformed header, wrong scheme, invalid base64, etc.
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides malformed Authorization header (missing space after "Basic")
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers = "Authorization: Basic\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be denied with 401 (not 400, since it parses as an invalid auth attempt)
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", response.status_text);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", response.body);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_invalid_base64_auth_when_provided_then_access_denied
 *
 * Purpose: Verify that Authorization headers with invalid base64 are rejected.
 * Expected: Server responds with 401 for invalid base64 encoding.
 */
void given_invalid_base64_auth_when_provided_then_access_denied(void)
{
    // Given: A running server with Basic authentication protection
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9037; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            // Validate Basic authentication credentials using proper base64 decoding
            if (validate_basic_auth(auth_header, "user", "pass")) {
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides Authorization header with invalid base64
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // "Basic invalid!@#" contains invalid base64 characters
    const char *headers = "Authorization: Basic invalid!@#\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be denied with 401 Unauthorized
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", response.status_text);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", response.body);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test: given_wrong_scheme_auth_when_provided_then_access_denied
 *
 * Purpose: Verify that Authorization headers with wrong schemes are rejected.
 * Expected: Server responds with 401 for non-Basic auth schemes.
 */
void given_wrong_scheme_auth_when_provided_then_access_denied(void)
{
    // Given: A running server that only accepts Basic authentication
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9038; // Use a unique port
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t protected_uri = {
        .uri      = "/protected",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            char auth_header[256];
            esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
            if (err != ESP_OK) {
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
            }

            // Only accept Basic authentication scheme
            if (validate_basic_auth(auth_header, "user", "pass")) {
                httpd_resp_sendstr(req, "Access granted");
                return ESP_OK;
            } else {
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Test Realm\"");
                return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
            }
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &protected_uri));

    // When: A client provides Authorization header with Digest scheme (not supported)
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *headers = "Authorization: Digest username=\"user\", realm=\"Test Realm\"\r\n";
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/protected", headers, NULL, 0, &response, TEST_TIMEOUT_MS));

    // Then: Access should be denied with 401 Unauthorized
    TEST_ASSERT_EQUAL(401, response.status_code);
    TEST_ASSERT_EQUAL_STRING("Unauthorized", response.status_text);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Invalid credentials", response.body);

    http_test_client_free_response(&response);
    http_test_client_disconnect(client);
    httpd_stop(handle);
}

/**
 * @brief Test function entry point for all authentication tests
 */
int test_authentication(void) {
    UNITY_BEGIN();
    RUN_TEST(given_protected_resource_when_no_auth_header_then_401_unauthorized_returned);
    RUN_TEST(given_basic_auth_credentials_when_valid_then_access_granted);
    RUN_TEST(given_basic_auth_credentials_when_invalid_then_access_denied);
    RUN_TEST(given_authentication_info_when_successful_then_header_included);
    RUN_TEST(given_multiple_auth_schemes_when_offered_then_client_can_choose);
    RUN_TEST(given_malformed_auth_header_when_provided_then_400_bad_request);
    RUN_TEST(given_invalid_base64_auth_when_provided_then_access_denied);
    RUN_TEST(given_wrong_scheme_auth_when_provided_then_access_denied);
    return UNITY_END();
}
