#include <unity.h>
#include <esp_http_server.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Required for setvbuf
#include "esp_httpd_priv.h" // For httpd_data, sock_db, httpd_req_aux, http_parser_url
#include "http_test_client.h" // Include for http_test_client

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // For getaddrinfo
#include <in6addr.h> // For in_port_t on Windows
#else
#include <sys/socket.h>
#include <netdb.h> // For getaddrinfo
#include <arpa/inet.h> // For inet_addr
#include <unistd.h> // for close
#include <netinet/in.h> // For in_port_t on Linux
#include <fcntl.h>
#endif

#define TAG "TEST_HTTPD_CLIENT"

/* Test timeout values */
#define TEST_TIMEOUT_MS 1000
#define TEST_BUFFER_SIZE 1024


/**
 * Test: given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds
 * 
 * Purpose: Verify client list retrieval functionality
 * Expected: httpd_get_client_list() returns ESP_OK and populates client fd array
 */
void given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds(void)
{
    // Given: Started HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8090;
    httpd_handle_t handle = NULL;
    esp_err_t start_ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, start_ret);
    
    // Prepare client FD array
    size_t fds = config.max_open_sockets;
    int * client_fds = (int*)malloc(sizeof(int) * fds);  // Array size should be >= max_open_sockets
    
    
    // When: Getting client list
    esp_err_t ret = httpd_get_client_list(handle, &fds, client_fds);
    
    // Then: Function succeeds and returns valid data
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT(fds <= config.max_open_sockets);  // fds count should not exceed max
    
    // Cleanup
    free(client_fds);
    httpd_stop(handle);
}


/**
 * Test: given_server_with_lru_enabled_when_max_sockets_exceeded_then_oldest_session_is_closed
 *
 * Purpose: Verify that the LRU (Least Recently Used) mechanism correctly closes the oldest session when the socket limit is exceeded.
 * Expected: The first client's connection is closed after the second client connects.
 */
void given_server_with_lru_enabled_when_max_sockets_exceeded_then_oldest_session_is_closed(void)
{
    // Given: A running server with LRU enabled and max_open_sockets = 1
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9002;
    config.max_open_sockets = 1;
    config.lru_purge_enable = true;
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: Client 1 connects
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int sockfd1 = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd1);
    TEST_ASSERT_EQUAL(0, connect(sockfd1, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    // Give server time to accept the connection
    httpd_os_thread_sleep(100);

    // When: Client 2 connects, exceeding the limit
    int sockfd2 = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, sockfd2);
    TEST_ASSERT_EQUAL(0, connect(sockfd2, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

    // Give server time to process the LRU logic
    httpd_os_thread_sleep(200);

    // Set a receive timeout for sockfd1 to prevent blocking indefinitely
    struct timeval tv;
    tv.tv_sec = 1; // 1 second timeout
    tv.tv_usec = 0;
    setsockopt(sockfd1, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    // Then: The connection for the first client should be closed by the server
    char buffer[32];
    int recv_ret = recv(sockfd1, buffer, sizeof(buffer), 0);

    // recv should return 0 (graceful close) or -1 (error/reset) on a closed socket
    TEST_ASSERT_LESS_OR_EQUAL(0, recv_ret);

    // And the second connection should still be active
    int send_ret = send(sockfd2, "ping", 4, 0);
    TEST_ASSERT_GREATER_THAN(0, send_ret);

    // Cleanup
#ifdef _WIN32
    closesocket(sockfd1);
    closesocket(sockfd2);
#else
    close(sockfd1);
    close(sockfd2);
#endif
    httpd_stop(handle);
}


static bool open_fn_invoked = false;
static esp_err_t mock_open_fn(httpd_handle_t hd, int sockfd) {
    (void)hd; (void)sockfd; // Unused parameters
    open_fn_invoked = true;
    return ESP_OK;
}

static bool close_fn_invoked = false;
static void mock_close_fn(httpd_handle_t hd, int sockfd) {
    (void)hd; (void)sockfd; // Unused parameters
    close_fn_invoked = true;
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
}



/**
 * Test: given_server_with_open_close_callbacks_when_client_connects_and_disconnects_then_callbacks_are_invoked
 *
 * Purpose: Verify that the open_fn and close_fn callbacks, configured in httpd_config_t, are correctly invoked
 *          during client connection and disconnection.
 * Expected: Both open_fn_invoked and close_fn_invoked flags are set to true.
 */
void given_server_with_open_close_callbacks_when_client_connects_and_disconnects_then_callbacks_are_invoked(void)
{
    // Given: A running server with open_fn and close_fn callbacks registered
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9006; // Use a unique port
    config.open_fn = mock_open_fn;
    config.close_fn = mock_close_fn;
    httpd_handle_t handle = NULL;

    open_fn_invoked = false;
    close_fn_invoked = false;

    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // When: A client connects and sends a request, then disconnects
    http_test_client_handle_t *client = http_test_client_init();
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_connect(client, "127.0.0.1", config.server_port, TEST_TIMEOUT_MS));

    // Send a request to ensure the connection is fully established and processed by the server
    http_test_response_t response = {0};
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_send_request(client, HTTP_METHOD_GET, "/test", NULL, NULL, 0, &response, TEST_TIMEOUT_MS));
    http_test_client_free_response(&response); // Free response body and headers

    // Disconnect the client
    TEST_ASSERT_EQUAL(HTTP_TEST_CLIENT_OK, http_test_client_disconnect(client));

    // Give server a moment to process the disconnection and invoke close_fn
    httpd_os_thread_sleep(200);

    // Then: Both open_fn and close_fn should have been invoked
    TEST_ASSERT_TRUE(open_fn_invoked);
    TEST_ASSERT_TRUE(close_fn_invoked);

    // Cleanup
    
    httpd_stop(handle);
}


/**
 * Test: given_server_with_multiple_clients_when_rapid_connections_then_server_handles_gracefully
 *
 * Purpose: Verify that the single-threaded server can handle multiple rapid connection attempts
 *          without crashing or mishandling connections, respecting max_open_sockets.
 * Expected: Connections up to max_open_sockets are accepted and processed; excess connections are rejected.
 */
void given_server_with_multiple_clients_when_rapid_connections_then_server_handles_gracefully(void)
{
    // Given: A running server with a limited number of open sockets
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9020; // Use a unique port
    config.max_open_sockets = 5; // Limit to 5 concurrent connections
    config.lru_purge_enable = false; // Disable LRU to test explicit rejection
    httpd_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, httpd_start(&handle, &config));

    // Register a simple handler for clients to request
    httpd_uri_t test_uri = {
        .uri      = "/test_concurrency",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            TEST_MESSAGE("GET /test_concurrency");
            httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        },
        .user_ctx = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, httpd_register_uri_handler(handle, &test_uri));

    // When: Multiple clients attempt to connect rapidly
    const int num_clients = 10; // More clients than max_open_sockets
    int client_sockets[num_clients];
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(config.server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    for (int i = 0; i < num_clients; ++i) {
        client_sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        TEST_ASSERT_GREATER_OR_EQUAL(0, client_sockets[i]);

        // Set non-blocking for connect to simulate rapid attempts
        #ifdef _WIN32
            u_long mode = 1; // 1 for non-blocking
            ioctlsocket(client_sockets[i], FIONBIO, &mode);
        #else
            fcntl(client_sockets[i], F_SETFL, O_NONBLOCK);
        #endif

        connect(client_sockets[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        // Don't check return value here, as it will likely be EINPROGRESS for non-blocking
    }

    // Give server time to process connections
    httpd_os_thread_sleep(200);

    // Then: Verify connections and responses
    int successful_connections = 0;
    for (int i = 0; i < num_clients; ++i) {
        // Switch back to blocking mode for recv
        #ifdef _WIN32
            u_long mode = 0; // 0 for blocking
            ioctlsocket(client_sockets[i], FIONBIO, &mode);
        #else
            fcntl(client_sockets[i], F_SETFL, 0);
        #endif

        // Set a short timeout for recv to avoid blocking indefinitely on rejected connections
        #ifdef _WIN32
            DWORD timeout = 100;
            setsockopt(client_sockets[i], SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        #else
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(client_sockets[i], SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        #endif

        const char *request = "GET /test_concurrency HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(client_sockets[i], request, strlen(request), 0);

        char buffer[1024] = {0};
        int recv_ret = recv(client_sockets[i], buffer, sizeof(buffer) - 1, 0);

        if (recv_ret > 0 && strstr(buffer, "HTTP/1.1 200 OK") != NULL) {
            successful_connections++;
        }
    }

    // Close sockets after checking all of them to ensure concurrency limit is tested
    for (int i = 0; i < num_clients; ++i) {
        #ifdef _WIN32
            closesocket(client_sockets[i]);
        #else
            close(client_sockets[i]);
        #endif
    }

    // Assert that the number of successful connections does not exceed max_open_sockets
    TEST_ASSERT_LESS_OR_EQUAL(config.max_open_sockets, successful_connections);
    // Also assert that at least some connections were successful (if server started correctly)
    TEST_ASSERT_GREATER_THAN(0, successful_connections);

    // Cleanup
    httpd_stop(handle);
}

int test_client_management(void) {
    // UNITY_BEGIN();
    RUN_TEST(given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds);
    RUN_TEST(given_server_with_lru_enabled_when_max_sockets_exceeded_then_oldest_session_is_closed);
    RUN_TEST(given_server_with_open_close_callbacks_when_client_connects_and_disconnects_then_callbacks_are_invoked);
    RUN_TEST(given_server_with_multiple_clients_when_rapid_connections_then_server_handles_gracefully);
    // return UNITY_END();
    return 0;
}
