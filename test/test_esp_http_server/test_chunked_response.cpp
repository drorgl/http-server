#include "unity.h"
#include "test_chunked_response.h"
#include <http_server.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

#define TEST_PORT 9016
#define TEST_TIMEOUT_MS 5000

static esp_err_t chunked_response_handler(httpd_req_t *req)
{
    esp_err_t err = httpd_start_chunked_response(req, "text/plain", NULL);
    if (err != ESP_OK) return err;

    const char *chunks[] = {"Hello", " ", "World!"};
    for (int i = 0; i < 3; i++) {
        size_t len = strlen(chunks[i]);
        err = httpd_send_chunk(req, chunks[i], len, NULL);
        if (err != ESP_OK) return err;
    }

    err = httpd_end_chunked_response(req, NULL);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

void test_chunked_response_basic(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = TEST_PORT;
    config.lru_purge_enable = false;
    config.max_open_sockets = 4;

    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&server, &config));

    httpd_uri_t chunked_test = {
        .uri       = "/chunked",
        .method    = HTTP_GET,
        .handler   = chunked_response_handler,
        .user_ctx  = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(server, &chunked_test));

    // Client: Connect and send GET request
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_MESSAGE(sock >= 0, "Failed to create socket");

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TEST_PORT);
    TEST_ASSERT_EQUAL(0, connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));

    const char *req = "GET /chunked HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Connection: close\r\n\r\n";
    int sent = send(sock, req, strlen(req), 0);
    TEST_ASSERT_EQUAL_MESSAGE((int)strlen(req), sent, "Send failed");

    sleep_ms(200);

    char buf[1024] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    TEST_ASSERT_GREATER_THAN(0, len);
    buf[len] = '\0';

    close(sock);

    // Assertions
    TEST_ASSERT_EQUAL_MESSAGE(200, strstr(buf, "200 OK") ? 200 : 0, "Status 200");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Transfer-Encoding: chunked"), "Chunked header");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "5\r\nHello\r\n"), "Hello chunk");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "1\r\n \r\n"), "Space chunk");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "6\r\nWorld!\r\n"), "World chunk");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "0\r\n\r\n"), "Final chunk");

    httpd_stop(server);
}

int test_chunked_response(void) {
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_chunked_response_basic);
    // return UnityEnd();
    return 0;
}
