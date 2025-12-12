
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
#define TAG "test_empty_header"

static esp_err_t test_header_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    char buf[100];

    if (httpd_req_get_hdr_value_str(req, "Header1", buf, sizeof(buf)) == ESP_OK) {
        if (strcmp("Value1", buf) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong value of Header1 received");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header1 not found");
        return ESP_FAIL;
    }

    if (httpd_req_get_hdr_value_str(req, "Header3", buf, sizeof(buf)) == ESP_OK) {
        if (strcmp("Value3", buf) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong value of Header3 received");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header3 not found");
        return ESP_FAIL;
    }

    if (httpd_req_get_hdr_value_str(req, "Header2", buf, sizeof(buf)) == ESP_OK) {
         if (strlen(buf) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header2 is not empty");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header2 not found");
        return ESP_FAIL;
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void given_server_with_empty_header_handler_when_client_sends_request_with_empty_header_then_it_is_handled_correctly(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9032;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    httpd_uri_t empty_header_uri = { };
    empty_header_uri.uri      = "/test_header";
    empty_header_uri.method   = HTTP_GET;
    empty_header_uri.handler  = test_header_get_handler;
    empty_header_uri.user_ctx = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &empty_header_uri));

    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    http_test_response_t response = {0};
    const char *headers = "Header1: Value1\r\nHeader2: \r\nHeader3: Value3\r\n";
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/test_header", headers, NULL, 0, &response, TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(200, response.status_code);
    TEST_ASSERT_NOT_NULL(response.body);
    TEST_ASSERT_EQUAL_STRING("OK", response.body);
    http_test_client_free_response(&response);

    http_test_client_disconnect(client);
    httpd_stop(handle);
}

int test_empty_header(void) {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);
    RUN_TEST(given_server_with_empty_header_handler_when_client_sends_request_with_empty_header_then_it_is_handled_correctly);
    // return UNITY_END();
    return 0;
}
