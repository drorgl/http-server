#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_httpd_priv.h"
#include "http_test_client.h"
#include "generic_event_group.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#endif

#define TEST_TIMEOUT_MS 2000
#define TAG "test_async_requests"

#define BIT0 (1 << 0)

static event_group_handle_t async_requests_event_group;
const int ASYNC_REQUEST_COMPLETED_BIT = BIT0;

static void async_response_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    const char *resp_str = "Hello from async task";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    httpd_req_async_handler_complete(req);
    event_group_set_bits(async_requests_event_group, ASYNC_REQUEST_COMPLETED_BIT);
    httpd_os_thread_delete();
}

static esp_err_t async_request_handler(httpd_req_t *req)
{
    httpd_req_t *async_req;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_req_async_handler_begin(req, &async_req));
    othread_t thread;
    httpd_os_thread_create(&thread, "async_response_task", 4096, 5, async_response_task, async_req, 0, 0);
    return ESP_OK;
}

void given_server_with_async_handler_when_client_requests_then_receives_response(void)
{
    async_requests_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(async_requests_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9028;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t async_uri = {
        .uri      = "/async",
        .method   = HTTP_GET,
        .handler  = async_request_handler,
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &async_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/async", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Hello from async task", response.body);
    http_test_client_free_response(&response);

    event_group_bits_t bits = event_group_wait_bits(async_requests_event_group, ASYNC_REQUEST_COMPLETED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & ASYNC_REQUEST_COMPLETED_BIT);

    http_test_client_disconnect(client);
    httpd_stop(handle);
    event_group_delete(async_requests_event_group);
}

int test_async_requests(void) {
    RUN_TEST(given_server_with_async_handler_when_client_requests_then_receives_response);
    return 0;
}
