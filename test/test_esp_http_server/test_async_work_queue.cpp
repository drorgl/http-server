
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
#include <errno.h>
#endif

#define TEST_TIMEOUT_MS 4000
#define TAG "test_async_work_queue"

static event_group_handle_t async_work_queue_event_group;
const int ASYNC_WORK_DONE_BIT = (1 << 0);

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

static void generate_async_resp(void *arg)
{
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;

    const char *resp_str = "Hello from async work";
    httpd_socket_send(hd, fd, resp_str, strlen(resp_str), 0);

    free(arg);
    event_group_set_bits(async_work_queue_event_group, ASYNC_WORK_DONE_BIT);
}

static esp_err_t async_get_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_send(req, "Hello World!", HTTPD_RESP_USE_STRLEN);

    struct async_resp_arg *resp_arg = (struct async_resp_arg *)malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    if (resp_arg->fd < 0) {
        return ESP_FAIL;
    }

    httpd_queue_work(req->handle, generate_async_resp, resp_arg);
    return ESP_OK;
}

void given_server_with_async_work_queue_handler_when_client_gets_then_receives_two_responses(void)
{
    async_work_queue_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(async_work_queue_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9031;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t async_uri = { };
    async_uri.uri      = "/async_work";
    async_uri.method   = HTTP_GET;
    async_uri.handler  = async_get_handler;
    async_uri.user_ctx = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &async_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/async_work", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("Hello World!", response.body);
    http_test_client_free_response(&response);

    event_group_wait_bits(async_work_queue_event_group, ASYNC_WORK_DONE_BIT, false, true, TEST_TIMEOUT_MS);

    char buffer[100] = {0};
    // Modified to handle recv return values and loop until data arrives or timeout
    int bytes_received = 0;
    int attempts = 0;
    const int max_attempts = 10; // Prevent infinite loop
    while (bytes_received == 0 && attempts < max_attempts) {
#ifdef _WIN32
        bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0);
#else
        bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // No data available yet, try again
            attempts++;
            usleep(100000); // 100ms delay before retry
            continue;
        }
#endif
        if (bytes_received < 0) {
            // Other error
            break;
        }
        attempts++;
    }

    // Ensure we received the expected string
    if (bytes_received > 0) {
        TEST_ASSERT_EQUAL_STRING("Hello from async work", buffer);
    } else {
        TEST_FAIL_MESSAGE("Failed to receive async response");
    }

    http_test_client_disconnect(client);
    httpd_stop(handle);
    event_group_delete(async_work_queue_event_group);
}

int test_async_work_queue(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_server_with_async_work_queue_handler_when_client_gets_then_receives_two_responses);
    // return UNITY_END();
    return 0;
}
