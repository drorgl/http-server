
#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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
#define TAG "test_session_context"

static event_group_handle_t session_context_event_group;
const int CONTEXT_FREE_BIT = (1 << 0);
static bool context_freed = false;

static void adder_free_func(void *ctx)
{
    LOGI(TAG, "Custom Free Context function called");
    free(ctx);
    context_freed = true;
    event_group_set_bits(session_context_event_group, CONTEXT_FREE_BIT);
}

static esp_err_t adder_post_handler(httpd_req_t *req)
{
    char buf[10];
    memset(buf, 0, sizeof(buf));
    char outbuf[50];
    memset(outbuf, 0, sizeof(outbuf));
    int ret;

    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    LOGI(TAG, "/adder handler read %d", val);

    if (!req->sess_ctx) {
        LOGI(TAG, "/adder allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        if (!req->sess_ctx)
        {
            return ESP_ERR_NO_MEM;
        }
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }
    int *adder = (int *)req->sess_ctx;
    *adder += val;

    LOGI(TAG, "adder ptr=%p, *adder=%d", adder, *adder);
    snprintf(outbuf, sizeof(outbuf),"%d", *adder);
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void given_server_with_session_handler_when_client_posts_then_context_is_maintained(void)
{
    session_context_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(session_context_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 0;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t adder_uri = { };
    adder_uri.uri      = "/adder";
    adder_uri.method   = HTTP_POST;
    adder_uri.handler  = adder_post_handler;
    adder_uri.user_ctx = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &adder_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    const char *body1 = "10";
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_POST, "/adder", NULL, body1, strlen(body1), &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("10", response.body);
    http_test_client_free_response(&response);

    const char *body2 = "15";
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_POST, "/adder", NULL, body2, strlen(body2), &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("25", response.body);
    http_test_client_free_response(&response);

    http_test_client_disconnect(client);

    event_group_wait_bits(session_context_event_group, CONTEXT_FREE_BIT, false, true, TEST_TIMEOUT_MS * 2);
    TEST_ASSERT_TRUE(context_freed);

    httpd_stop(handle);
    event_group_delete(session_context_event_group);
}


int test_session_context(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_server_with_session_handler_when_client_posts_then_context_is_maintained);
    // return UNITY_END();
    return 0;
}
