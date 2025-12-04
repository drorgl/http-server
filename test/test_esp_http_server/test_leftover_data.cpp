
#include <unity.h>
#include <http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "http_test_client.h"

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
#define TAG "test_leftover_data"

static esp_err_t leftover_data_post_handler(httpd_req_t *req)
{
    /* Only echo the first 10 bytes of the request, leaving the rest of the
     * request data as is.
     */
    char buf[11];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    LOGI(TAG, "leftover data handler read %s", buf);
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void given_server_with_leftover_data_handler_when_client_posts_then_server_handles_it_gracefully(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9030;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t leftover_data_uri = { };
    leftover_data_uri.uri      = "/leftover_data";
    leftover_data_uri.method   = HTTP_POST;
    leftover_data_uri.handler  = leftover_data_post_handler;
    leftover_data_uri.user_ctx = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &leftover_data_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    const char *body = "12345678901234567890";
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_POST, "/leftover_data", NULL, body, strlen(body), &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("1234567890", response.body);
    http_test_client_free_response(&response);

    // Send another request to make sure the server is still alive and the connection is not corrupted
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_POST, "/leftover_data", NULL, body, strlen(body), &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("1234567890", response.body);
    http_test_client_free_response(&response);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

int test_leftover_data(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_server_with_leftover_data_handler_when_client_posts_then_server_handles_it_gracefully);
    // return UNITY_END();
    return 0;
}
