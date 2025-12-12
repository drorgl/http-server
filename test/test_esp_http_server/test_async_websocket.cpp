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
#define TAG "test_async_websocket"

#define BIT0 (1 << 0)
#define BIT1 (1 << 1)
#define BIT2 (1 << 2)
#define BIT3 (1 << 3)

static event_group_handle_t ws_event_group;
const int WS_CONNECTED_BIT = BIT0;
const int WS_DISCONNECTED_BIT = BIT1;
const int WS_FRAME_SENT_BIT = BIT2;
const int WS_SEND_FAILED_BIT = BIT3;

static esp_err_t ws_async_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        event_group_set_bits(ws_event_group, WS_CONNECTED_BIT);
        
        httpd_ws_frame_t ws_pkt;
        uint8_t *buf = NULL;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        while (1) {
            // Receive frame to keep the connection open and handle control frames
            esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
            if (ret != ESP_OK) {
                break;
            }

            if (ws_pkt.len) {
                buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
                if (buf == NULL) {
                    return ESP_ERR_NO_MEM;
                }
                ws_pkt.payload = buf;
                ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
                if (ret != ESP_OK) {
                    free(buf);
                    break;
                }
                free(buf);
            }
        }
        event_group_set_bits(ws_event_group, WS_DISCONNECTED_BIT);
        return ESP_OK;
    }
    return ESP_OK;
}

static void send_data_task_sync(void *arg)
{
    httpd_handle_t handle = ((void **)arg)[0];
    int fd = *((int *)((void **)arg)[1]);
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)"Hello from sync task";
    ws_pkt.len = strlen((char *)ws_pkt.payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_send_data(handle, fd, &ws_pkt);
    if (ret == ESP_OK) {
        event_group_set_bits(ws_event_group, WS_FRAME_SENT_BIT);
    }
    httpd_os_thread_delete();
}

void given_ws_connection_when_sending_sync_from_another_task_then_succeeds(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9024;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri = "/ws_async",
        .method = HTTP_GET,
        .handler = ws_async_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    // A real implementation would generate this key
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_async", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    size_t fds_count = 1;
    int client_fds[1];
    TEST_ASSERT_EQUAL(ESP_OK, httpd_get_client_list(handle, &fds_count, client_fds));
    TEST_ASSERT_EQUAL(1, fds_count);

    void *task_args[] = {handle, &client_fds[0]};
    othread_t thread;
    httpd_os_thread_create(&thread, "send_data_task_sync", 4096, 5, send_data_task_sync, &task_args, 0, 0);

    ws_test_frame_t received_frame;
    memset(&received_frame, 0, sizeof(received_frame));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(WS_TYPE_TEXT, received_frame.type);
    TEST_ASSERT_EQUAL_STRING("Hello from sync task", (char*)received_frame.payload);

    bits = event_group_wait_bits(ws_event_group, WS_FRAME_SENT_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_FRAME_SENT_BIT);

    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

static void async_transfer_complete_cb(esp_err_t err, int socket, void *arg)
{
    if (err == ESP_OK) {
        event_group_set_bits(ws_event_group, WS_FRAME_SENT_BIT);
    }
}

static void send_data_task_async(void *arg)
{
    httpd_handle_t handle = ((void **)arg)[0];
    int fd = *((int *)((void **)arg)[1]);
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)"Hello from async task";
    ws_pkt.len = strlen((char *)ws_pkt.payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_data_async(handle, fd, &ws_pkt, async_transfer_complete_cb, NULL);
    httpd_os_thread_delete();
}

void given_ws_connection_when_sending_async_from_another_task_then_succeeds(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9025;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri = "/ws_async",
        .method = HTTP_GET,
        .handler = ws_async_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_async", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    size_t fds_count = 1;
    int client_fds[1];
    TEST_ASSERT_EQUAL(ESP_OK, httpd_get_client_list(handle, &fds_count, client_fds));
    TEST_ASSERT_EQUAL(1, fds_count);

    void *task_args[] = {handle, &client_fds[0]};
    othread_t thread;
    httpd_os_thread_create(&thread, "send_data_task_async", 4096, 5, send_data_task_async, &task_args, 0, 0);

    ws_test_frame_t received_frame;
    memset(&received_frame, 0, sizeof(received_frame));
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_recv_frame(client, &received_frame, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(WS_TYPE_TEXT, received_frame.type);
    TEST_ASSERT_EQUAL_STRING("Hello from async task", (char*)received_frame.payload);

    bits = event_group_wait_bits(ws_event_group, WS_FRAME_SENT_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_FRAME_SENT_BIT);

    ws_test_client_free_frame(&received_frame);
    http_test_client_disconnect(client);
    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

static void send_data_task_sync_fail(void *arg)
{
    httpd_handle_t handle = ((void **)arg)[0];
    int fd = *((int *)((void **)arg)[1]);
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)"Hello";
    ws_pkt.len = strlen((char *)ws_pkt.payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_os_thread_sleep(100); // Give client time to close
    esp_err_t ret = httpd_ws_send_data(handle, fd, &ws_pkt);
    if (ret != ESP_OK) {
        event_group_set_bits(ws_event_group, WS_SEND_FAILED_BIT);
    }
    httpd_os_thread_delete();
}

void given_closed_ws_connection_when_sending_sync_then_fails(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9026;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri = "/ws_async",
        .method = HTTP_GET,
        .handler = ws_async_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_async", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    size_t fds_count = 1;
    int client_fds[1];
    TEST_ASSERT_EQUAL(ESP_OK, httpd_get_client_list(handle, &fds_count, client_fds));
    TEST_ASSERT_EQUAL(1, fds_count);

    void *task_args[] = {handle, &client_fds[0]};
    othread_t thread;
    httpd_os_thread_create(&thread, "send_data_task_sync_fail", 4096, 5, send_data_task_sync_fail, &task_args, 0, 0);

    http_test_client_disconnect(client);

    bits = event_group_wait_bits(ws_event_group, WS_SEND_FAILED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_SEND_FAILED_BIT);

    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

static void async_transfer_complete_cb_fail(esp_err_t err, int socket, void *arg)
{
    if (err != ESP_OK) {
        event_group_set_bits(ws_event_group, WS_SEND_FAILED_BIT);
    }
}

static void send_data_task_async_fail(void *arg)
{
    httpd_handle_t handle = ((void **)arg)[0];
    int fd = *((int *)((void **)arg)[1]);
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)"Hello";
    ws_pkt.len = strlen((char *)ws_pkt.payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_os_thread_sleep(100); // Give client time to close
    httpd_ws_send_data_async(handle, fd, &ws_pkt, async_transfer_complete_cb_fail, NULL);
    httpd_os_thread_delete();
}

void given_closed_ws_connection_when_sending_async_then_callback_receives_error(void)
{
    ws_event_group = event_group_create();
    TEST_ASSERT_NOT_NULL(ws_event_group);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9027;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t ws_uri = {
        .uri = "/ws_async",
        .method = HTTP_GET,
        .handler = ws_async_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &ws_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    const char *client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    char expected_accept_key[33];
    strcpy(expected_accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, ws_test_client_handshake(client, "/ws_async", "127.0.0.1", client_key, expected_accept_key, TEST_TIMEOUT_MS));

    event_group_bits_t bits = event_group_wait_bits(ws_event_group, WS_CONNECTED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_CONNECTED_BIT);

    size_t fds_count = 1;
    int client_fds[1];
    TEST_ASSERT_EQUAL(ESP_OK, httpd_get_client_list(handle, &fds_count, client_fds));
    TEST_ASSERT_EQUAL(1, fds_count);

    void *task_args[] = {handle, &client_fds[0]};
    othread_t thread;
    httpd_os_thread_create(&thread, "send_data_task_async_fail", 4096, 5, send_data_task_async_fail, &task_args, 0, 0);

    http_test_client_disconnect(client);

    bits = event_group_wait_bits(ws_event_group, WS_SEND_FAILED_BIT, false, true, TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE(bits & WS_SEND_FAILED_BIT);

    httpd_stop(handle);
    event_group_delete(ws_event_group);
}

int test_async_websocket(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_ws_connection_when_sending_sync_from_another_task_then_succeeds);
    RUN_TEST(given_ws_connection_when_sending_async_from_another_task_then_succeeds);
    RUN_TEST(given_closed_ws_connection_when_sending_sync_then_fails);
    RUN_TEST(given_closed_ws_connection_when_sending_async_then_callback_receives_error);
    // return UNITY_END();
    return 0;
}
