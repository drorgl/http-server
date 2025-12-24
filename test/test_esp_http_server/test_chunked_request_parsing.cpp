#include "unity.h"
#include "test_chunked_request_parsing.h"
#include <http_server.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_httpd_priv.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define sleep_ms(ms) Sleep(ms)
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#define sleep_ms(ms) usleep((ms)*1000)
#endif

#include "httpd_chunked.h"

static const char * TAG = "test_chunked_request_parsing";

#define TEST_PORT 9017
#define TEST_TIMEOUT_MS 5000

static esp_err_t chunked_request_handler(httpd_req_t *req)
{
    struct httpd_req_aux *ra = (struct httpd_req_aux *)req->aux;
    httpd_chunked_ctx_t *ctx = ra->chunk_ctx;

    char buf[1024];
    size_t bytes_read;
    size_t total = 0;
    esp_err_t err;
    while ((err = httpd_read_chunk(req, ctx, buf, sizeof(buf), &bytes_read)) == ESP_OK) {
        total += bytes_read;
        LOGD(TAG, LOG_FMT("bytes_read=%zu, total=%zu"), bytes_read, total);
    }
    LOGD(TAG, LOG_FMT("final total=%zu, last err=%d"), total, err);

    char resp[64];
    snprintf(resp, sizeof(resp), "OK: %zu bytes", total);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t invalid_chunk_handler(httpd_req_t *req)
{
    httpd_chunked_ctx_t ctx = {0};
    esp_err_t err = httpd_parse_chunked_request(req, &ctx);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid chunk");
        return ESP_FAIL;
    }
    // If init succeeds, attempt to read chunks in a loop
    char buf[16];
    size_t bytes_read;
    err = ESP_OK;
    while (err == ESP_OK) {
        err = httpd_read_chunk(req, &ctx, buf, sizeof(buf), &bytes_read);
    }
    if (err == HTTPD_ERR_CHUNK_SIZE_INVALID) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid chunk size");
        return ESP_FAIL;
    }
    // If reads succeed unexpectedly, send "OK" (to fail test - test expects 400)
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t trailer_security_handler(httpd_req_t *req)
{
    httpd_chunked_ctx_t ctx = {0};
    esp_err_t err = httpd_parse_chunked_request(req, &ctx);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid chunk");
        return ESP_FAIL;
    }
    // Attempt to read all chunks
    char buf[16];
    size_t bytes_read;
    err = ESP_OK;
    while (err == ESP_OK) {
        err = httpd_read_chunk(req, &ctx, buf, sizeof(buf), &bytes_read);
    }
    if (err == ESP_ERR_INVALID_ARG) {
        // Security violation in trailers
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid trailer");
        return ESP_FAIL;
    }
    // If reads end normally, send "OK"
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

void test_chunked_request_basic(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = TEST_PORT;
    config.lru_purge_enable = false;

    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&server, &config));

    httpd_uri_t chunked_test = {
        .uri       = "/chunked_req",
        .method    = HTTP_POST,
        .handler   = chunked_request_handler,
        .user_ctx  = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(server, &chunked_test));

    // Client sends chunked POST
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_MESSAGE(sock >= 0, "Failed to create socket");

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TEST_PORT);
    TEST_ASSERT_EQUAL(0, connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));

    const char *chunked_req = "POST /chunked_req HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Transfer-Encoding: chunked\r\n"
                              "Connection: close\r\n\r\n"
                              "5\r\nhello\r\n"
                              "6\r\n world\r\n"
                              "0\r\n\r\n";
    int sent = send(sock, chunked_req, strlen(chunked_req), 0);
    TEST_ASSERT_EQUAL_MESSAGE((int)strlen(chunked_req), sent, "Send chunked request failed");

    sleep_ms(200);

    char buf[1024] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    LOGD(TAG, LOG_FMT("recv len = %d"), len);
    if (len > 0) {
        LOGD_BUFFER_HEXDUMP(TAG, buf, len, "received data");
    }
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "OK: 11 bytes"));

    close(sock);
    httpd_stop(server);
}

void test_chunked_request_invalid_size(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = TEST_PORT + 1;
    config.lru_purge_enable = false;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&server, &config));

    httpd_uri_t bad_chunk = {
        .uri       = "/bad_chunk",
        .method    = HTTP_POST,
        .handler   = invalid_chunk_handler,
        .user_ctx  = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(server, &bad_chunk));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_MESSAGE(sock >= 0, "Failed to create socket");

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TEST_PORT + 1);
    TEST_ASSERT_EQUAL(0, connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));

    const char *invalid_req = "POST /bad_chunk HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Transfer-Encoding: chunked\r\n"
                              "Connection: close\r\n\r\n"
                              "g\r\ninvalid\r\n"
                              "0\r\n\r\n";
    int sent = send(sock, invalid_req, strlen(invalid_req), 0);
    TEST_ASSERT_EQUAL_MESSAGE((int)strlen(invalid_req), sent, "Send invalid request failed");

    sleep_ms(200);

    char buf[1024] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "400 Bad Request"));

    close(sock);
    httpd_stop(server);
}

void test_chunked_request_trailer_security(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = TEST_PORT + 2;
    config.lru_purge_enable = false;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&server, &config));

    httpd_uri_t bad_trailer = {
        .uri       = "/bad_trailer",
        .method    = HTTP_POST,
        .handler   = trailer_security_handler,
        .user_ctx  = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(server, &bad_trailer));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_MESSAGE(sock >= 0, "Failed to create socket");

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TEST_PORT + 2);
    TEST_ASSERT_EQUAL(0, connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));

    const char *invalid_req = "POST /bad_trailer HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Transfer-Encoding: chunked\r\n"
                              "Connection: close\r\n\r\n"
                              "5\r\nhello\r\n"
                              "0\r\n"
                              "Trailer: bad\r\nvalue\r\n"
                              "\r\n";
    int sent = send(sock, invalid_req, strlen(invalid_req), 0);
    TEST_ASSERT_EQUAL_MESSAGE((int)strlen(invalid_req), sent, "Send invalid request failed");

    sleep_ms(200);

    char buf[1024] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "400 Bad Request"));

    close(sock);
    httpd_stop(server);
}

void test_chunked_request_uppercase_hex(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = TEST_PORT + 3;
    config.lru_purge_enable = false;

    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&server, &config));

    httpd_uri_t chunked_test = {
        .uri       = "/chunked_upper",
        .method    = HTTP_POST,
        .handler   = chunked_request_handler,
        .user_ctx  = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(server, &chunked_test));

    // Client sends chunked POST with uppercase hex
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_MESSAGE(sock >= 0, "Failed to create socket");

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TEST_PORT + 3);
    TEST_ASSERT_EQUAL(0, connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));

    const char *chunked_req = "POST /chunked_upper HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Transfer-Encoding: chunked\r\n"
                              "Connection: close\r\n\r\n"
                              "A\r\nhello worl\r\n"
                              "0\r\n\r\n";
    int sent = send(sock, chunked_req, strlen(chunked_req), 0);
    TEST_ASSERT_EQUAL_MESSAGE((int)strlen(chunked_req), sent, "Send chunked request failed");

    sleep_ms(200);

    char buf[1024] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    LOGD(TAG, LOG_FMT("recv len = %d"), len);
    if (len > 0) {
        LOGD_BUFFER_HEXDUMP(TAG, buf, len, "received data");
    }
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "OK: 10 bytes"));

    close(sock);
    httpd_stop(server);
}

int test_chunked_request_parsing(void) {
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_chunked_request_basic);
    RUN_TEST(test_chunked_request_invalid_size);
    RUN_TEST(test_chunked_request_trailer_security);
    RUN_TEST(test_chunked_request_uppercase_hex);
    // return UnityEnd();
    return 0;
}
