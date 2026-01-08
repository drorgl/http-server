#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <http_server.h>
#include "http_server_middleware.h"
#include "http_test_client.h"
#include "middleware_content_negotiation.h"

#include <esp_httpd_priv.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef ESP_PLATFORM
#include "port/esp32/osal.h"
#endif

// Define httpd_os_thread_sleep for native tests if not available
#ifndef ESP_PLATFORM
static void httpd_os_thread_sleep(int msecs) {
    // Simple sleep implementation for native tests
    // For Windows
    #ifdef _WIN32
        Sleep(msecs);
    #else
        usleep(msecs * 1000);
    #endif
}
#endif

static esp_err_t set_content_type_mock(httpd_req_t *req, const char *content_type) {
    // For now, just return OK (Phase 1 - basic functionality)
    return ESP_OK;
}

static esp_err_t add_vary_header_mock(httpd_req_t *req, const char *vary_value) {
    // For now, just return OK (Phase 1 - basic functionality)
    return ESP_OK;
}

static httpd_content_negotiation_config_t content_negotiation_cfg = {
    .capabilities = {
        .media_types = (char*[]){"application/json", "text/html", "text/plain", NULL},
        .media_type_count = 3,
        .encodings = (char*[]){"gzip", "deflate", "identity", NULL},
        .encoding_count = 3,
        .languages = (char*[]){"en", "es", "fr", NULL},
        .language_count = 3,
        .charsets = (char*[]){"utf-8", "iso-8859-1", NULL},
        .charset_count = 2
    },
    .get_capabilities = NULL, // Static capabilities for test
    .context = NULL,
    .free_ctx = NULL,
    .set_content_type = set_content_type_mock,
    .add_vary_header = add_vary_header_mock,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str
};

static httpd_middleware_config_t negotiation_middleware_cfg = {
    .func = middleware_content_negotiation,
    .context = &content_negotiation_cfg,
    .free_ctx = NULL,
    .priority = 0,
    .uri_pattern = "/test",
    .method_filter = HTTP_ANY,
    .enabled = true,
    .uri_match_wildcard = httpd_uri_match_wildcard
};

/**
 * @brief Simple echo handler for testing
 */
static esp_err_t echo_handler(httpd_req_t *req) {
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Start test server with content negotiation middleware
 */
static httpd_handle_t start_test_server_with_negotiation(uint16_t *port_out) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;  // Use dynamic port
    // config.ctrl_port = 0;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    httpd_handle_t server;
    esp_err_t ret = httpd_start(&server, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    if (ret != ESP_OK) {
        return NULL;
    }

    // Define the base URI for /test
    static httpd_uri_t base_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = echo_handler,
        .user_ctx = NULL
    };

    // Wrap the handler with content negotiation middleware
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&base_uri, &negotiation_middleware_cfg, 1);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    ret = httpd_register_uri_handler(server, wrapped_uri);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_os_thread_sleep(200);  // Allow server to start

    // Get server port
    // We use a proxy structure to access the first member (config) of httpd_data
    // without including private headers that might cause issues on some platforms
    struct {
        httpd_config_t config;
    } *hd_proxy = (void *)server;
    *port_out = hd_proxy->config.server_port;

    return server;
}

/**
 * @brief Stop test server and cleanup
 */
static void stop_test_server(httpd_handle_t server) {
    httpd_unregister_uri_handler(server, "/test", HTTP_GET);
    httpd_stop(server);
}

/**
 * @brief Test that middleware initializes correctly with content negotiation
 */
void test_e2e_cn_middleware_initialization(void) {
    httpd_handle_t server;
    uint16_t port;

    // Start server with content negotiation middleware
    server = start_test_server_with_negotiation(&port);
    TEST_ASSERT_NOT_NULL(server);
    TEST_ASSERT_TRUE(port > 0);

    // For ESP_PLATFORM, verify server is running on expected port
#ifdef ESP_PLATFORM
    struct httpd_data *hd = (struct httpd_data *)server;
    TEST_ASSERT_TRUE(hd->config.server_port == port);
#endif

    // Connect client using HTTP test client
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Send request without Accept header - should work (fallback)
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);

    // Test with Accept header - middleware should parse it without error
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *accept_hdr = "Accept: application/json, text/html;q=0.8\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", accept_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);  // Should still work
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);

    // Cleanup
    stop_test_server(server);
}

/**
 * @brief Test middleware with malformed Accept headers
 */
void test_e2e_content_negotiation_malformed_headers(void) {
    httpd_handle_t server;
    uint16_t port;

    // Start server with content negotiation middleware
    server = start_test_server_with_negotiation(&port);
    TEST_ASSERT_NOT_NULL(server);

    // Connect client
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Test various malformed Accept headers
    const char *malformed_headers[] = {
        "Accept: invalid;q=1.5\r\n",     // Invalid quality value
        "Accept: text/html;q=-1\r\n",     // Negative quality
        "Accept: type;q=abc\r\n",         // Non-numeric quality
        "Accept: ;\r\n",                  // Empty ranges
        "Accept: ,,\r\n",                 // Empty ranges with commas
        NULL
    };

    for (int i = 0; malformed_headers[i] != NULL; i++) {
        http_test_response_t resp = {0};
        http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test",
                                                                malformed_headers[i], NULL, 0, &resp, 5000);
        TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK, "Malformed header should not crash server");
        TEST_ASSERT_EQUAL(200, resp.status_code);  // Should still return OK

        http_test_client_free_response(&resp);
    }

    http_test_client_disconnect(client);
    stop_test_server(server);
}

/**
 * @brief Test middleware configuration edge cases
 */
void test_e2e_content_negotiation_server_config(void) {
    httpd_handle_t server;
    uint16_t port;

    // Start server with content negotiation middleware
    server = start_test_server_with_negotiation(&port);
    TEST_ASSERT_NOT_NULL(server);

    // Connect client and make multiple requests to ensure stability
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Make several requests to test middleware consistency
    for (int i = 0; i < 5; i++) {
        http_test_response_t resp = {0};
        http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test",
                                                                NULL, NULL, 0, &resp, 5000);
        TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, err);
        TEST_ASSERT_EQUAL(200, resp.status_code);
        TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

        http_test_client_free_response(&resp);
    }

    http_test_client_disconnect(client);
    stop_test_server(server);
}


/**
 * @brief Main test function for E2E content negotiation
 */
int run_test_e2e_content_negotiation(void) {
    UnitySetTestFile(__FILE__);

    // E2E middleware integration tests
    RUN_TEST(test_e2e_cn_middleware_initialization);
    RUN_TEST(test_e2e_content_negotiation_malformed_headers);
    RUN_TEST(test_e2e_content_negotiation_server_config);

    return 0;
}
