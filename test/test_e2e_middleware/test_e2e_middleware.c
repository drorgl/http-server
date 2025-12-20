#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <http_server.h>
#include "esp_httpd_priv.h"
#include "http_server_middleware.h"
#include "middleware_auth.h"
#include "middleware_cors.h"
#include "middleware_logging.h"
#include "middleware_range.h"
#include "middleware_conditional.h"
#include "middleware_content_negotiation.h"
#include "http_test_client.h"
#include "test_e2e_content_negotiation.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#if defined(__linux__)
#include <signal.h>
#endif

void setUp(void) {
}

void tearDown(void) {
}

static esp_err_t echo_handler(httpd_req_t *req) {
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static const char test_range_content[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";
static const long long test_range_content_len = sizeof(test_range_content) - 1; // Exclude null terminator

static esp_err_t range_test_handler(httpd_req_t *req, const httpd_range_response_ctx_t *ctx) {
    const httpd_range_request_t *range_req = ctx->request;

    // For this test, we expect single ranges only since multiple ranges are disabled
    if (range_req->range_count == 1) {
        const httpd_range_spec_t *spec = &range_req->ranges[0];

        long long start_pos = spec->has_start ? spec->start : 0;
        long long end_pos = spec->has_end ? spec->end : (test_range_content_len - 1);

        // Ensure bounds
        if (start_pos < 0) start_pos = 0;
        if (end_pos >= test_range_content_len) end_pos = test_range_content_len - 1;
        if (start_pos > end_pos) {
            return ESP_FAIL; // This should not happen if validation is correct
        }

        size_t content_len = (size_t)(end_pos - start_pos + 1);
        const char *content = test_range_content + start_pos;

        // Send partial content response
        esp_err_t ret = httpd_resp_set_type(req, "text/plain");
        if (ret != ESP_OK) return ret;

        return httpd_resp_send_partial_content(req, content, content_len, start_pos, end_pos, test_range_content_len);
    }

    return ESP_FAIL;
}

static httpd_range_middleware_config_t range_cfg = {
    .handler = range_test_handler,
    .context = NULL,
    .free_ctx = NULL,
    .content_type = "text/plain",
    .content_length = test_range_content_len,
    .enable_multiple_ranges = false
};

static esp_err_t test_auth_check(const char *username, const char *password, void *ctx) {
    return (username && password && strcmp(username, "testuser") == 0 && strcmp(password, "testpass") == 0) ? ESP_OK : ESP_FAIL;
}

static bool test_auth_requires(const char *uri, void *ctx) {
    return strcmp(uri, "/test") == 0;
}

static auth_config_t auth_cfg = {
    .check_credentials = test_auth_check,
    .check_ctx = NULL,
    .requires_auth = test_auth_requires,
    .bypass_ctx = NULL,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .resp_set_status = httpd_resp_set_status,
    .resp_set_hdr = httpd_resp_set_hdr,
    .resp_send_err = httpd_resp_send_err
};

static cors_config_t cors_cfg = {
    .allowed_origins = "*",
    .allowed_methods = "GET,POST,OPTIONS",
    .allowed_headers = "*",
    .allow_credentials = true,
    .max_age = 3600,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .resp_set_status = httpd_resp_set_status,
    .resp_set_hdr = httpd_resp_set_hdr,
    .resp_send_err = httpd_resp_send_err,
    .resp_send = httpd_resp_send
};

static logging_config_t log_cfg = {
    .log_level = 1,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .method_str = http_method_str, // Use the http_method_str from http_parser.h
    .printf = (int (*)(const char *, ...))printf
};

// Test data for conditional middleware
static const char test_conditional_content[] = "Test content for conditional requests";
static const long long test_conditional_timestamp = 1609459200; // 2021-01-01 00:00:00 GMT

static esp_err_t test_etag_generator(httpd_req_t *req, char *etag, size_t etag_len) {
    // Generate ETag based on test content
    return httpd_generate_strong_etag(test_conditional_content, strlen(test_conditional_content), etag, etag_len);
}

static esp_err_t test_last_modified_fn(httpd_req_t *req, long long *last_modified) {
    *last_modified = test_conditional_timestamp;
    return ESP_OK;
}

static httpd_conditional_middleware_config_t conditional_cfg = {
    .etag_generator = test_etag_generator,
    .last_modified_fn = test_last_modified_fn,
    .context = NULL,
    .free_ctx = NULL,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .req_get_hdr_value_len = httpd_req_get_hdr_value_len,
    .resp_set_status = httpd_resp_set_status,
    .resp_set_hdr = httpd_resp_set_hdr,
    .resp_send = httpd_resp_send
};

// Content Negotiation middleware test configuration
static esp_err_t mock_set_content_type(httpd_req_t *req, const char *content_type) {
    // For now, just return OK (Phase 1 - no actual negotiation)
    return ESP_OK;
}

static esp_err_t mock_add_vary_header(httpd_req_t *req, const char *vary_value) {
    // For now, just return OK (Phase 1 - no actual negotiation)
    return ESP_OK;
}

static httpd_content_negotiation_config_t content_negotiation_cfg = {
    .capabilities = {
        .media_types = (char*[]){"application/json", "text/html", "text/plain", NULL},
        .encodings = (char*[]){"gzip", "deflate", "identity", NULL},
        .languages = (char*[]){"en", "es", "fr", NULL},
        .charsets = (char*[]){"utf-8", "iso-8859-1", NULL}
    },
    .get_capabilities = NULL, // Static capabilities for test
    .context = NULL,
    .free_ctx = NULL,
    .set_content_type = mock_set_content_type,
    .add_vary_header = mock_add_vary_header,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str
};

static esp_err_t start_test_server(const httpd_middleware_config_t *configs, size_t num_configs, uint16_t *port_out, httpd_handle_t *handle_out, httpd_uri_t **wrapped_out, uint16_t ctrl_port) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 0;
    cfg.ctrl_port = ctrl_port;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;

    esp_err_t ret = httpd_start(handle_out, &cfg);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    if (ret != ESP_OK) return ret;

    static httpd_uri_t s_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = echo_handler,
        .user_ctx = NULL
    };

    *wrapped_out = httpd_uri_wrap_with_middleware(&s_uri, configs, num_configs);
    TEST_ASSERT_NOT_NULL(*wrapped_out);
    if (!*wrapped_out) {
        httpd_stop(*handle_out);
        return ESP_FAIL;
    }

    ret = httpd_register_uri_handler(*handle_out, *wrapped_out);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    httpd_os_thread_sleep(200);

    struct httpd_data *hd = (struct httpd_data *)*handle_out;
    *port_out = hd->config.server_port;
    printf("Test server started on port %d\n", *port_out);
    return ESP_OK;
}

static void stop_test_server(httpd_handle_t handle, httpd_uri_t *wrapped) {
    httpd_unregister_uri_handler(handle, "/test", HTTP_GET);
    free(wrapped);
    httpd_stop(handle);
}

void test_e2e_auth_only(void) {
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_auth,
            .context = &auth_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32768));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // No auth expect 401
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for no-auth");
    TEST_ASSERT_EQUAL(401, resp.status_code);
    const char *www_auth = http_test_client_get_header(&resp, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL(www_auth);
    TEST_ASSERT(strstr(www_auth, "Basic") != NULL);
    free((void*)www_auth);
    http_test_client_free_response(&resp);

    // Disconnect and reconnect for the second request since Connection: close was sent
    http_test_client_disconnect(client);

    // Valid auth expect 200 OK
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *auth_hdr = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_auth_only_null_uri_match(void) {
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_auth,
            .context = &auth_cfg,
            .enabled = true,
            .uri_match_wildcard = NULL,  // NULL callback - should use default implementation
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32769));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // No auth expect 401 (middleware should still work with NULL callback)
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for no-auth");
    TEST_ASSERT_EQUAL(401, resp.status_code);
    const char *www_auth = http_test_client_get_header(&resp, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL(www_auth);
    TEST_ASSERT(strstr(www_auth, "Basic") != NULL);
    free((void*)www_auth);
    http_test_client_free_response(&resp);

    // Disconnect and reconnect for the second request
    http_test_client_disconnect(client);

    // Valid auth expect 200 OK
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *auth_hdr = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_cors_only(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_cors,
            .context = &cors_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32770));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *origin_hdr = "Origin: http://localhost:3000\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", origin_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    const char *acao = http_test_client_get_header(&resp, "Access-Control-Allow-Origin");
    TEST_ASSERT_NOT_NULL(acao);
    TEST_ASSERT(strstr(acao, "*") != NULL || strcmp(acao, "http://localhost:3000") == 0);
    free((void*)acao);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_logging_only(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_logging,
            .context = &log_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32771));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_normal_request(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32773));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_valid_single_range(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32774));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=10-20\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(206, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("KLMNOPQRSTU", resp.body, resp.body_len); // chars at positions 10-20
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes 10-20/62", content_range);
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL(accept_ranges);
    TEST_ASSERT_EQUAL_STRING("bytes", accept_ranges);
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_valid_suffix_range(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32775));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=-10\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(206, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("qrstuvwxyz", resp.body, resp.body_len); // last 10 chars: qrstuvwxyz (positions 52-61)
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes 52-61/62", content_range);
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL(accept_ranges);
    TEST_ASSERT_EQUAL_STRING("bytes", accept_ranges);
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_valid_open_ended_range(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32776));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=50-\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(206, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("opqrstuvwxyz", resp.body, resp.body_len); // chars from position 50 to end: opqrstuvwxyz (positions 50-61)
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes 50-61/62", content_range);
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL(accept_ranges);
    TEST_ASSERT_EQUAL_STRING("bytes", accept_ranges);
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_invalid_malformed_range(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32777));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=abc-123\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for malformed range");
    TEST_ASSERT_EQUAL(416, resp.status_code);
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes */62", content_range);
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_invalid_out_of_bounds(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32778));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=100-200\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for out-of-bounds range");
    TEST_ASSERT_EQUAL(416, resp.status_code);
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes */62", content_range);
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_invalid_start_greater_than_end(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32779));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=30-20\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for invalid range");
    TEST_ASSERT_EQUAL(416, resp.status_code);
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes */62", content_range);
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

void test_e2e_range_multiple_ranges_disabled(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_range,
            .context = &range_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32780));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *range_hdr = "Range: bytes=10-20,30-40\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for multiple ranges");
    TEST_ASSERT_EQUAL(416, resp.status_code);
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL(content_range);
    TEST_ASSERT_EQUAL_STRING("bytes */62", content_range);
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

// Conditional middleware e2e tests
void test_e2e_conditional_normal_request(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32781));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    // Check ETag and Last-Modified headers are set
    const char *etag = http_test_client_get_header(&resp, "ETag");
    TEST_ASSERT_NOT_NULL(etag);
    TEST_ASSERT_TRUE(strstr(etag, "\"") != NULL); // Should be quoted
    free((void*)etag);

    const char *last_modified = http_test_client_get_header(&resp, "Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified);
    free((void*)last_modified);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_match_matching(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32782));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // First request to get the ETag
    http_test_response_t resp1 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp1.status_code);

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL(etag);

    // Second request with If-Match header using the ETag
    http_test_client_free_response(&resp1);

    char if_match_hdr[256];
    snprintf(if_match_hdr, sizeof(if_match_hdr), "If-Match: %s\r\n", etag);
    free((void*)etag);
    // Disconnect and reconnect
    http_test_client_disconnect(client);
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp2 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_match_hdr, NULL, 0, &resp2, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp2.status_code); // Should continue normally
    TEST_ASSERT_EQUAL_MEMORY("OK", resp2.body, resp2.body_len);

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_match_non_matching(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32783));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Request with non-matching If-Match header
    const char *if_match_hdr = "If-Match: \"non-matching-etag\"\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_match_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for non-matching If-Match");
    TEST_ASSERT_EQUAL(412, resp.status_code);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_none_match_get(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32784));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // First request to get the ETag
    http_test_response_t resp1 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp1.status_code);

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL(etag);
    http_test_client_free_response(&resp1);

    // Second request with If-None-Match header using the ETag
    http_test_client_disconnect(client);
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    char if_none_match_hdr[256];
    snprintf(if_none_match_hdr, sizeof(if_none_match_hdr), "If-None-Match: %s\r\n", etag);
    free((void*)etag);

    http_test_response_t resp2 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_none_match_hdr, NULL, 0, &resp2, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp2.status_code != 0, "No response for If-None-Match GET");
    TEST_ASSERT_EQUAL(304, resp2.status_code); // GET with matching ETag returns 304 Not Modified

    // Check ETag and Last-Modified headers are present in 304 response
    const char *etag_304 = http_test_client_get_header(&resp2, "ETag");
    TEST_ASSERT_NOT_NULL(etag_304);
    free((void*)etag_304);

    const char *last_modified_304 = http_test_client_get_header(&resp2, "Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified_304);
    free((void*)last_modified_304);

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_none_match_post(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32785));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // First request to get the ETag
    http_test_response_t resp1 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp1.status_code);

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL(etag);
    http_test_client_free_response(&resp1);

    // POST request with If-None-Match header using the ETag
    http_test_client_disconnect(client);
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    char if_none_match_hdr[256];
    snprintf(if_none_match_hdr, sizeof(if_none_match_hdr), "If-None-Match: %s\r\n", etag);
    free((void*)etag);

    http_test_response_t resp2 = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_POST, "/test", if_none_match_hdr, NULL, 0, &resp2, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp2.status_code != 0, "No response for If-None-Match POST");
    TEST_ASSERT_EQUAL(405, resp2.status_code); // POST method not allowed on this URI - method validation happens before conditional evaluation

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_modified_since(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32786));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Request with If-Modified-Since that matches the last modified time
    const char *if_modified_since_hdr = "If-Modified-Since: Fri, 01 Jan 2021 00:00:00 GMT\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_modified_since_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for If-Modified-Since");
    TEST_ASSERT_EQUAL(304, resp.status_code); // Should return 304 Not Modified

    // Check headers are present
    const char *etag = http_test_client_get_header(&resp, "ETag");
    TEST_ASSERT_NOT_NULL(etag);
    free((void*)etag);

    const char *last_modified = http_test_client_get_header(&resp, "Last-Modified");
    TEST_ASSERT_NOT_NULL(last_modified);
    free((void*)last_modified);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_conditional_if_unmodified_since(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_conditional,
            .context = &conditional_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32787));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Request with If-Unmodified-Since that is earlier than last modified time
    const char *if_unmodified_since_hdr = "If-Unmodified-Since: Thu, 31 Dec 2020 23:59:59 GMT\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_unmodified_since_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for If-Unmodified-Since");
    TEST_ASSERT_EQUAL(412, resp.status_code); // Should return 412 Precondition Failed

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

// Content Negotiation middleware e2e tests
void test_e2e_content_negotiation_basic_integration(void) {
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_content_negotiation,
            .context = &content_negotiation_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32788));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Normal request without Accept headers should pass through
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_content_negotiation_accept_header_parsing(void) {
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_content_negotiation,
            .context = &content_negotiation_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32789));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Request with various Accept headers - middleware should parse without error
    const char *accept_hdr = "Accept: text/html, application/json;q=0.8\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", accept_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code); // Should still pass through in Phase 1
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);

    // Test with malformed Accept header
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *malformed_accept_hdr = "Accept: invalid;header;\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", malformed_accept_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code); // Should handle gracefully
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_content_negotiation_malformed_headers1(void) {
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_content_negotiation,
            .context = &content_negotiation_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32790));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Test with various malformed Accept headers that should not crash the middleware
    const char *test_headers[] = {
        "Accept: ;\r\n",                    // Empty ranges
        "Accept: ,,\r\n",                  // Empty ranges with commas
        "Accept: type;q=abc\r\n",          // Invalid quality value
        "Accept: type;q=1.5\r\n",          // Quality value > 1.0
        "Accept: type;q=-0.1\r\n",         // Negative quality value
        "Accept: \t \n\r\n",               // Whitespace only
        NULL
    };

    for (int i = 0; test_headers[i] != NULL; i++) {
        http_test_response_t resp = {0};
        http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", test_headers[i], NULL, 0, &resp, 5000);
        TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code == 200, "Malformed header should not crash server");

        http_test_client_free_response(&resp);

        // Brief pause between requests
        httpd_os_thread_sleep(10);
    }

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100);
}

void test_e2e_all_three(void) {
    http_test_client_err_t err;
    httpd_handle_t handle;
    uint16_t port;
    httpd_uri_t *wrapped = NULL;
    httpd_middleware_config_t configs[] = {
        {
            .func = middleware_logging,
            .context = &log_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        },
        {
            .func = middleware_cors,
            .context = &cors_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        },
        {
            .func = middleware_auth,
            .context = &auth_cfg,
            .enabled = true,
            .uri_match_wildcard = httpd_uri_match_wildcard,
            .uri_pattern = "/test",
            .method_filter = HTTP_ANY
        }
    };
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 3, &port, &handle, &wrapped, 32772));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *headers = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\nOrigin: http://localhost:3000\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", headers, NULL, 0, &resp, 5000);
    TEST_ASSERT(err == HTTP_TEST_CLIENT_OK);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_MEMORY("OK", resp.body, resp.body_len);
    const char *acao = http_test_client_get_header(&resp, "Access-Control-Allow-Origin");
    TEST_ASSERT_NOT_NULL(acao);
    free((void*)acao);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
    httpd_os_thread_sleep(100); // Allow time for socket closure
}

int test_e2e_middleware(void) {
    UNITY_BEGIN();
    RUN_TEST(test_e2e_auth_only);
    RUN_TEST(test_e2e_auth_only_null_uri_match);
    RUN_TEST(test_e2e_cors_only);
    RUN_TEST(test_e2e_logging_only);
    RUN_TEST(test_e2e_range_normal_request);
    RUN_TEST(test_e2e_range_valid_single_range);
    RUN_TEST(test_e2e_range_valid_suffix_range);
    RUN_TEST(test_e2e_range_valid_open_ended_range);
    RUN_TEST(test_e2e_range_invalid_malformed_range);
    RUN_TEST(test_e2e_range_invalid_out_of_bounds);
    RUN_TEST(test_e2e_range_invalid_start_greater_than_end);
    RUN_TEST(test_e2e_range_multiple_ranges_disabled);
    RUN_TEST(test_e2e_conditional_normal_request);
    RUN_TEST(test_e2e_conditional_if_match_matching);
    RUN_TEST(test_e2e_conditional_if_match_non_matching);
    RUN_TEST(test_e2e_conditional_if_none_match_get);
    RUN_TEST(test_e2e_conditional_if_none_match_post);
    RUN_TEST(test_e2e_conditional_if_modified_since);
    RUN_TEST(test_e2e_conditional_if_unmodified_since);
    RUN_TEST(test_e2e_content_negotiation_basic_integration);
    RUN_TEST(test_e2e_content_negotiation_accept_header_parsing);
    RUN_TEST(test_e2e_content_negotiation_malformed_headers1);
    RUN_TEST(test_e2e_all_three);
    run_test_e2e_content_negotiation();
    return UNITY_END();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

#if defined(__linux__)
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        printf("Failed to set SIGPIPE handler\n");
    }
#endif

    int ret = test_e2e_middleware();

#ifdef _WIN32
    WSACleanup();
#endif

    return ret;
}

void app_main(void) {
    main();
}
