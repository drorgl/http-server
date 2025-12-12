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
#include "http_test_client.h"

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
    RUN_TEST(test_e2e_all_three);
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
