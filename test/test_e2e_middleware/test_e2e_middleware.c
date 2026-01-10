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
#include "log.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#if defined(__linux__)
#include <signal.h>
#endif

#define TAG "test_e2e_middleware"
#define TEST_LOGD(fmt, ...)    LOGD(TAG, "TEST: " fmt, ##__VA_ARGS__)
#define TEST_LOGW(fmt, ...)    LOGW(TAG, "TEST: " fmt, ##__VA_ARGS__)
#define TEST_LOGE(fmt, ...)    LOGE(TAG, "TEST: " fmt, ##__VA_ARGS__)

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
            TEST_LOGE("Invalid range: start %lld > end %lld", start_pos, end_pos);
            return ESP_FAIL; // This should not happen if validation is correct
        }

        size_t content_len = (size_t)(end_pos - start_pos + 1);
        const char *content = test_range_content + start_pos;

        // Send partial content response
        esp_err_t ret = httpd_resp_set_type(req, "text/plain");
        if (ret != ESP_OK) return ret;

        return httpd_resp_send_partial_content(req, content, content_len, start_pos, end_pos, test_range_content_len);
    }

    TEST_LOGE("Unexpected range count: %zu", range_req->range_count);
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
    if (!username || !password) {
        TEST_LOGD("Auth check failed: missing username or password");
        return ESP_FAIL;
    }
    bool match = strcmp(username, "testuser") == 0 && strcmp(password, "testpass") == 0;
    TEST_LOGD("Auth check: user='%s', pass='%s' -> %s", username, password, match ? "MATCH" : "NO MATCH");
    return match ? ESP_OK : ESP_FAIL;
}

static bool test_auth_requires(const char *uri, void *ctx) {
    bool requires = strcmp(uri, "/test") == 0;
    TEST_LOGD("Auth required for URI '%s' -> %s", uri, requires ? "YES" : "NO");
    return requires;
}

static auth_config_t auth_cfg = {
    .check_credentials = test_auth_check,
    .requires_auth = test_auth_requires,
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
    .method_str = http_method_str,
    .printf = (int (*)(const char *, ...))printf
};

// Test data for conditional middleware
static const char test_conditional_content[] = "Test content for conditional requests";
static const long long test_conditional_timestamp = 1609459200; // 2021-01-01 00:00:00 GMT

static esp_err_t test_etag_generator(httpd_req_t *req, char *etag, size_t etag_len) {
    esp_err_t ret = httpd_generate_strong_etag(test_conditional_content, strlen(test_conditional_content), etag, etag_len);
    TEST_LOGD("Generated ETag: '%s' (len %zu)", etag, strlen(etag));
    return ret;
}

static esp_err_t test_last_modified_fn(httpd_req_t *req, long long *last_modified) {
    *last_modified = test_conditional_timestamp;
    TEST_LOGD("Last-Modified timestamp: %lld (%s)", *last_modified, ctime((time_t*)last_modified));
    return ESP_OK;
}

static httpd_conditional_middleware_config_t conditional_cfg = {
    .etag_generator = test_etag_generator,
    .last_modified_fn = test_last_modified_fn,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .req_get_hdr_value_len = httpd_req_get_hdr_value_len,
    .resp_set_status = httpd_resp_set_status,
    .resp_set_hdr = httpd_resp_set_hdr,
    .resp_send = httpd_resp_send
};

// Content Negotiation middleware test configuration
static esp_err_t mock_set_content_type(httpd_req_t *req, const char *content_type) {
    // For now, just return OK as we haven't implemented content negotiation logic yet
    TEST_LOGD("Mock setting content-type: %s (ignored)", content_type ? content_type : "NULL");
    return ESP_OK;
}

static esp_err_t mock_add_vary_header(httpd_req_t *req, const char *vary_value) {
    TEST_LOGD("Mock adding Vary header: %s (ignored)", vary_value ? vary_value : "NULL");
    return ESP_OK;
}

static httpd_content_negotiation_config_t content_negotiation_cfg = {
    .capabilities = {
        .media_types = (char*[]){"application/json", "text/html", "text/plain", NULL},
        .encodings = (char*[]){"gzip", "deflate", "identity", NULL},
        .languages = (char*[]){"en", "es", "fr", NULL},
        .charsets = (char*[]){"utf-8", "iso-8859-1", NULL}
    },
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
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Failed to start HTTP server");
    if (ret != ESP_OK) return ret;

    static httpd_uri_t s_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = echo_handler,
        .user_ctx = NULL
    };

    *wrapped_out = httpd_uri_wrap_with_middleware(&s_uri, configs, num_configs);
    TEST_ASSERT_NOT_NULL_MESSAGE(*wrapped_out, "Failed to wrap URI with middleware");
    if (!*wrapped_out) {
        httpd_stop(*handle_out);
        return ESP_FAIL;
    }

    ret = httpd_register_uri_handler(*handle_out, *wrapped_out);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Failed to register URI handler");

    httpd_os_thread_sleep(200);

    struct httpd_data *hd = (struct httpd_data *)*handle_out;
    *port_out = hd->config.server_port;
    TEST_LOGD("Test server started on port %d", *port_out);
    return ESP_OK;
}

static void stop_test_server(httpd_handle_t handle, httpd_uri_t *wrapped) {
    TEST_LOGD("Stopping test server");
    httpd_unregister_uri_handler(handle, "/test", HTTP_GET);
    if (httpd_is_wrapped_handler(wrapped)) {
        httpd_free_wrapped_ctx(wrapped->user_ctx);
    }
    free(wrapped);
    httpd_stop(handle);
}

void test_e2e_auth_only(void) {
    TEST_MESSAGE("Testing authentication middleware: basic auth requirement");

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

    // Test: No auth should return 401
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for no-auth request");
    TEST_ASSERT_EQUAL_MESSAGE(401, resp.status_code, "Expected 401 Unauthorized for request without auth");
    const char *www_auth = http_test_client_get_header(&resp, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL_MESSAGE(www_auth, "WWW-Authenticate header should be present");
    TEST_LOGW("WWW-Authenticate: %s", www_auth);
    TEST_ASSERT_MESSAGE(strstr(www_auth, "Basic") != NULL, "WWW-Authenticate should contain 'Basic'");
    free((void*)www_auth);
    http_test_client_free_response(&resp);

    // Disconnect and reconnect for second request (due to Connection: close)
    http_test_client_disconnect(client);

    // Test: Valid auth should return 200 OK
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *auth_hdr = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\n"; // testuser:testpass base64
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Valid auth request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Valid auth should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_auth_only_null_uri_match(void) {
    TEST_MESSAGE("Testing authentication middleware with NULL URI match callback (should use default)");

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

    // Test: No auth with NULL callback should still return 401 (middleware should work with NULL callback)
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for no-auth with NULL callback");
    TEST_ASSERT_EQUAL_MESSAGE(401, resp.status_code, "Expected 401 with NULL URI match callback");
    const char *www_auth = http_test_client_get_header(&resp, "WWW-Authenticate");
    TEST_ASSERT_NOT_NULL_MESSAGE(www_auth, "WWW-Authenticate header should be present");
    TEST_ASSERT_MESSAGE(strstr(www_auth, "Basic") != NULL, "WWW-Authenticate should contain 'Basic'");
    free((void*)www_auth);
    http_test_client_free_response(&resp);

    // Disconnect and reconnect for second request
    http_test_client_disconnect(client);

    // Test: Valid auth should return 200 OK
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *auth_hdr = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Valid auth request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Valid auth should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_cors_only(void) {
    TEST_MESSAGE("Testing CORS middleware: preflight and origin handling");

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
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "CORS request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "CORS request should return 200 OK");
    const char *acao = http_test_client_get_header(&resp, "Access-Control-Allow-Origin");
    TEST_ASSERT_NOT_NULL_MESSAGE(acao, "Access-Control-Allow-Origin header should be present");
    bool wildcard_match = strstr(acao, "*") != NULL;
    bool origin_match = strcmp(acao, "http://localhost:3000") == 0;
    TEST_ASSERT_MESSAGE(wildcard_match || origin_match, "Access-Control-Allow-Origin should match '*' or the requested origin");
    TEST_LOGW("Access-Control-Allow-Origin: %s", acao);
    free((void*)acao);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_logging_only(void) {
    TEST_MESSAGE("Testing logging middleware: request logging");

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
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Logging middleware request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Request with logging should return 200 OK");
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_normal_request(void) {
    TEST_MESSAGE("Testing range middleware: normal request (no range headers)");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32772));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Normal range request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Normal range request should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_valid_single_range(void) {
    TEST_MESSAGE("Testing range middleware: valid single range request (bytes=10-20)");

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

    const char *range_hdr = "Range: bytes=10-20\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Range request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(206, resp.status_code, "Valid range should return 206 Partial Content");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("KLMNOPQRSTU", resp.body, resp.body_len, "Should return chars at positions 10-20");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes 10-20/62", content_range, "Content-Range should show correct range");
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL_MESSAGE(accept_ranges, "Accept-Ranges header should be present");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes", accept_ranges, "Accept-Ranges should be 'bytes'");
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_valid_suffix_range(void) {
    TEST_MESSAGE("Testing range middleware: valid suffix range request (bytes=-10)");

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

    const char *range_hdr = "Range: bytes=-10\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Suffix range request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(206, resp.status_code, "Valid suffix range should return 206 Partial Content");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("qrstuvwxyz", resp.body, resp.body_len, "Should return last 10 chars");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes 52-61/62", content_range, "Content-Range should show correct suffix range");
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL_MESSAGE(accept_ranges, "Accept-Ranges header should be present");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes", accept_ranges, "Accept-Ranges should be 'bytes'");
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_valid_open_ended_range(void) {
    TEST_MESSAGE("Testing range middleware: valid open-ended range request (bytes=50-)");

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

    const char *range_hdr = "Range: bytes=50-\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Open-ended range request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(206, resp.status_code, "Valid open-ended range should return 206 Partial Content");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("opqrstuvwxyz", resp.body, resp.body_len, "Should return chars from 50 to end");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes 50-61/62", content_range, "Content-Range should show correct open-ended range");
    free((void*)content_range);
    const char *accept_ranges = http_test_client_get_header(&resp, "Accept-Ranges");
    TEST_ASSERT_NOT_NULL_MESSAGE(accept_ranges, "Accept-Ranges header should be present");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes", accept_ranges, "Accept-Ranges should be 'bytes'");
    free((void*)accept_ranges);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_invalid_malformed_range(void) {
    TEST_MESSAGE("Testing range middleware: invalid malformed range request (bytes=abc-123)");

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

    const char *range_hdr = "Range: bytes=abc-123\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for malformed range header");
    TEST_ASSERT_EQUAL_MESSAGE(416, resp.status_code, "Malformed range should return 416 Range Not Satisfiable");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present for 416");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes */62", content_range, "Content-Range should show unsatisfied range");
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_invalid_out_of_bounds(void) {
    TEST_MESSAGE("Testing range middleware: invalid out-of-bounds range request (bytes=100-200)");

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

    const char *range_hdr = "Range: bytes=100-200\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for out-of-bounds range");
    TEST_ASSERT_EQUAL_MESSAGE(416, resp.status_code, "Out-of-bounds range should return 416 Range Not Satisfiable");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present for 416");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes */62", content_range, "Content-Range should show unsatisfied range");
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_invalid_start_greater_than_end(void) {
    TEST_MESSAGE("Testing range middleware: invalid range with start > end (bytes=30-20)");

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

    const char *range_hdr = "Range: bytes=30-20\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for invalid range");
    TEST_ASSERT_EQUAL_MESSAGE(416, resp.status_code, "Invalid range should return 416 Range Not Satisfiable");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present for 416");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes */62", content_range, "Content-Range should show unsatisfied range");
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_range_multiple_ranges_disabled(void) {
    TEST_MESSAGE("Testing range middleware: multiple ranges when disabled (bytes=10-20,30-40)");

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

    const char *range_hdr = "Range: bytes=10-20,30-40\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", range_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for multiple ranges when disabled");
    TEST_ASSERT_EQUAL_MESSAGE(416, resp.status_code, "Multiple ranges when disabled should return 416 Range Not Satisfiable");
    const char *content_range = http_test_client_get_header(&resp, "Content-Range");
    TEST_ASSERT_NOT_NULL_MESSAGE(content_range, "Content-Range header should be present for 416");
    TEST_LOGW("Content-Range: %s", content_range);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bytes */62", content_range, "Content-Range should show unsatisfied range");
    free((void*)content_range);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

// Conditional middleware e2e tests
void test_e2e_conditional_normal_request(void) {
    TEST_MESSAGE("Testing conditional middleware: normal request (no conditional headers)");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32780));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Normal conditional request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Normal request should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");

    // Check ETag and Last-Modified headers are set
    const char *etag = http_test_client_get_header(&resp, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag, "ETag header should be set");
    TEST_LOGW("ETag header: %s", etag);
    TEST_ASSERT_MESSAGE(strstr(etag, "\"") != NULL, "ETag should be quoted");
    free((void*)etag);

    const char *last_modified = http_test_client_get_header(&resp, "Last-Modified");
    TEST_ASSERT_NOT_NULL_MESSAGE(last_modified, "Last-Modified header should be set");
    TEST_LOGW("Last-Modified header: %s", last_modified);
    free((void*)last_modified);

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_match_matching(void) {
    TEST_MESSAGE("Testing conditional middleware: If-Match with matching ETag (should allow request)");

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

    // First request to obtain the ETag from the response
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err1, "First request to get ETag should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp1.status_code, "First request should return 200 OK");

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag, "ETag should be available from first response");
    TEST_LOGW("Obtained ETag for If-Match test: %s", etag);
    http_test_client_free_response(&resp1);

    // Disconnect and reconnect for second request
    http_test_client_disconnect(client);

    // Second request with If-Match header using the retrieved ETag
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    char if_match_hdr[256];
    int hdr_len = snprintf(if_match_hdr, sizeof(if_match_hdr), "If-Match: %s\r\n", etag);
    TEST_ASSERT_MESSAGE(hdr_len > 0 && hdr_len < (int)sizeof(if_match_hdr), "If-Match header format failed");
    free((void*)etag);

    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_match_hdr, NULL, 0, &resp2, 5000);
    TEST_LOGD("If-Match request completed with status: %d", resp2.status_code);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err2, "If-Match matching request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp2.status_code, "If-Match with matching ETag should allow request (200 OK)");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp2.body, resp2.body_len, "Response body should be 'OK'");

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_match_non_matching(void) {
    TEST_MESSAGE("Testing conditional middleware: If-Match with non-matching ETag (should return 412)");

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

    // Request with non-matching If-Match header
    const char *if_match_hdr = "If-Match: \"fake-etag\"\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_match_hdr, NULL, 0, &resp, 5000);
    TEST_LOGD("If-Match non-matching request completed with status: %d", resp.status_code);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for non-matching If-Match");
    TEST_ASSERT_EQUAL_MESSAGE(412, resp.status_code, "If-Match with non-matching ETag should return 412 Precondition Failed");

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_none_match_get(void) {
    TEST_MESSAGE("Testing conditional middleware: If-None-Match GET with matching ETag (should return 304)");

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

    // First request to obtain the ETag from the response
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err1, "First request to get ETag should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp1.status_code, "First request should return 200 OK");

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag, "ETag should be available from first response");
    TEST_LOGW("Obtained ETag for If-None-Match GET test: %s", etag);
    http_test_client_free_response(&resp1);

    // Disconnect and reconnect for second request
    http_test_client_disconnect(client);

    // GET request with If-None-Match header using the retrieved ETag
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    char if_none_match_hdr[256];
    int hdr_len = snprintf(if_none_match_hdr, sizeof(if_none_match_hdr), "If-None-Match: %s\r\n", etag);
    TEST_ASSERT_MESSAGE(hdr_len > 0 && hdr_len < (int)sizeof(if_none_match_hdr), "If-None-Match header format failed");
    free((void*)etag);

    http_test_response_t resp2 = {0};
    http_test_client_err_t err2 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_none_match_hdr, NULL, 0, &resp2, 5000);
    TEST_ASSERT_MESSAGE(err2 == HTTP_TEST_CLIENT_OK || resp2.status_code != 0, "No response for If-None-Match GET");
    TEST_ASSERT_EQUAL_MESSAGE(304, resp2.status_code, "GET with matching ETag in If-None-Match should return 304 Not Modified");

    // Check ETag and Last-Modified headers are present in 304 response
    const char *etag_304 = http_test_client_get_header(&resp2, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag_304, "ETag header should be present in 304 response");
    free((void*)etag_304);

    const char *last_modified_304 = http_test_client_get_header(&resp2, "Last-Modified");
    TEST_ASSERT_NOT_NULL_MESSAGE(last_modified_304, "Last-Modified header should be present in 304 response");
    free((void*)last_modified_304);

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_none_match_post(void) {
    TEST_MESSAGE("Testing conditional middleware: If-None-Match POST (should return 405 since method not allowed)");

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

    // First request to obtain the ETag from the response
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err1, "First request to get ETag should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp1.status_code, "First request should return 200 OK");

    const char *etag = http_test_client_get_header(&resp1, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag, "ETag should be available from first response");
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
    http_test_client_err_t err2 = http_test_client_send_request(client, HTTP_METHOD_POST, "/test", if_none_match_hdr, NULL, 0, &resp2, 5000);
    TEST_ASSERT_MESSAGE(err2 == HTTP_TEST_CLIENT_OK || resp2.status_code != 0, "No response for If-None-Match POST");
    TEST_ASSERT_EQUAL_MESSAGE(405, resp2.status_code, "POST method not allowed on this URI - method validation happens before conditional evaluation");

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_modified_since(void) {
    TEST_MESSAGE("Testing conditional middleware: If-Modified-Since with matching date (should return 304)");

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

    // First request to obtain the Last-Modified timestamp from the response
    http_test_response_t resp1 = {0};
    http_test_client_err_t err1 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp1, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err1, "First request to get Last-Modified should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp1.status_code, "First request should return 200 OK");

    const char *last_modified_str = http_test_client_get_header(&resp1, "Last-Modified");
    TEST_ASSERT_NOT_NULL_MESSAGE(last_modified_str, "Last-Modified header should be available from first response");
    TEST_LOGW("Obtained Last-Modified for If-Modified-Since test: %s", last_modified_str);

    http_test_response_t resp2 = {0};

    // Use the Last-Modified header as the If-Modified-Since header
    char if_modified_since_hdr[256];
    int hdr_len = snprintf(if_modified_since_hdr, sizeof(if_modified_since_hdr), "If-Modified-Since: %s\r\n", last_modified_str);
    TEST_ASSERT_MESSAGE(hdr_len > 0 && hdr_len < (int)sizeof(if_modified_since_hdr), "If-Modified-Since header format failed");
    free((void*)last_modified_str);
    http_test_client_free_response(&resp1);

    // Disconnect and reconnect for second request
    http_test_client_disconnect(client);
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    http_test_client_err_t err2 = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_modified_since_hdr, NULL, 0, &resp2, 5000);
    TEST_LOGD("If-Modified-Since request completed with status: %d", resp2.status_code);
    TEST_ASSERT_MESSAGE(err2 == HTTP_TEST_CLIENT_OK || resp2.status_code != 0, "No response for If-Modified-Since");
    TEST_ASSERT_EQUAL_MESSAGE(304, resp2.status_code, "If-Modified-Since with matching date should return 304 Not Modified");

    // Check headers are present in 304 response
    const char *etag_304 = http_test_client_get_header(&resp2, "ETag");
    TEST_ASSERT_NOT_NULL_MESSAGE(etag_304, "ETag header should be present in 304 response");
    free((void*)etag_304);

    const char *last_modified_304 = http_test_client_get_header(&resp2, "Last-Modified");
    TEST_ASSERT_NOT_NULL_MESSAGE(last_modified_304, "Last-Modified header should be present in 304 response");
    free((void*)last_modified_304);

    http_test_client_free_response(&resp2);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_conditional_if_unmodified_since(void) {
    TEST_MESSAGE("Testing conditional middleware: If-Unmodified-Since with earlier date (should return 412)");

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

    // Request with If-Unmodified-Since that is earlier than the resource's last modified time
    const char *if_unmodified_since_hdr = "If-Unmodified-Since: Thu, 31 Dec 2020 23:59:59 GMT\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", if_unmodified_since_hdr, NULL, 0, &resp, 5000);
    TEST_LOGD("If-Unmodified-Since request completed with status: %d", resp.status_code);
    TEST_ASSERT_MESSAGE(err == HTTP_TEST_CLIENT_OK || resp.status_code != 0, "No response for If-Unmodified-Since");
    TEST_ASSERT_EQUAL_MESSAGE(412, resp.status_code, "If-Unmodified-Since with earlier date should return 412 Precondition Failed");

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

// Content Negotiation middleware e2e tests
void test_e2e_content_negotiation_basic_integration(void) {
    TEST_MESSAGE("Testing content negotiation middleware: basic integration (no Accept headers - passthrough)");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32787));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Normal request without Accept headers should pass through
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Content negotiation passthrough should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Content negotiation request should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_content_negotiation_accept_header_parsing(void) {
    TEST_MESSAGE("Testing content negotiation middleware: Accept header parsing (passthrough with logging)");

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

    // Request with various Accept headers - middleware should parse without error
    const char *accept_hdr = "Accept: text/html, application/json;q=0.8\r\n";
    http_test_response_t resp = {0};
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", accept_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Content negotiation with Accept header should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Content negotiation should still pass through in Phase 1");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);

    // Test with malformed Accept header
    client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *malformed_accept_hdr = "Accept: invalid;header;\r\n";
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", malformed_accept_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Malformed Accept header should be handled gracefully");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Malformed Accept header should not cause server failure");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");

    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}



void test_e2e_all_three(void) {
    TEST_MESSAGE("Testing all three middleware types together: logging, CORS, auth");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 3, &port, &handle, &wrapped, 32791));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    const char *headers = "Authorization: Basic dGVzdHVzZXI6dGVzdHBhc3M=\r\nOrigin: http://localhost:3000\r\n";
    http_test_response_t resp = {0};
    err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", headers, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(HTTP_TEST_CLIENT_OK, err, "Combined middleware request should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(200, resp.status_code, "Combined middleware should return 200 OK");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("OK", resp.body, resp.body_len, "Response body should be 'OK'");
    const char *acao = http_test_client_get_header(&resp, "Access-Control-Allow-Origin");
    TEST_ASSERT_NOT_NULL_MESSAGE(acao, "Access-Control-Allow-Origin header should be present");
    free((void*)acao);
    http_test_client_free_response(&resp);

    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

// Additional auth e2e tests for edge cases
void test_e2e_auth_invalid_base64(void) {
    TEST_MESSAGE("Testing auth middleware: invalid base64 in Authorization header (should return 401)");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32792));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Invalid base64 in Authorization header
    http_test_response_t resp = {0};
    const char *auth_hdr = "Authorization: Basic invalidbase64!!\r\n";
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(401, resp.status_code, "Invalid base64 should return 401 Unauthorized");
    TEST_LOGW("Invalid base64 auth returned 401 as expected");
    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

void test_e2e_auth_malformed_header(void) {
    TEST_MESSAGE("Testing auth middleware: malformed Authorization header (should return 401)");

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
    TEST_ASSERT_EQUAL(ESP_OK, start_test_server(configs, 1, &port, &handle, &wrapped, 32793));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_NOT_NULL(wrapped);

    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", port, 5000));

    // Malformed Auth header - no Basic
    http_test_response_t resp = {0};
    const char *auth_hdr = "Authorization: Bearer token\r\n";
    http_test_client_err_t err = http_test_client_send_request(client, HTTP_METHOD_GET, "/test", auth_hdr, NULL, 0, &resp, 5000);
    TEST_ASSERT_EQUAL_MESSAGE(401, resp.status_code, "Malformed auth header should return 401 Unauthorized");
    TEST_LOGW("Malformed auth header returned 401 as expected");
    http_test_client_free_response(&resp);
    http_test_client_disconnect(client);
    stop_test_server(handle, wrapped);
}

int test_e2e_middleware(void) {
    UNITY_BEGIN();
    TEST_LOGD("Starting e2e middleware test suite");
    RUN_TEST(test_e2e_auth_only);
    RUN_TEST(test_e2e_auth_only_null_uri_match);
    RUN_TEST(test_e2e_auth_invalid_base64);
    RUN_TEST(test_e2e_auth_malformed_header);
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
    RUN_TEST(test_e2e_all_three);
    run_test_e2e_content_negotiation();
    TEST_LOGD("Completed e2e middleware test suite");
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
