#include <unity.h>
#include <stdio.h> // Required for setvbuf

#ifdef _WIN32
#include <winsock2.h>
#elif defined(__linux__)
#include <signal.h>
#endif

#include <stdio.h>

#include "test_server_lifecycle.h"
#include "test_uri_handlers.h"
#include "test_request_processing.h"
#include "test_response_handling.h"
#include "test_websocket.h"
#include "test_client_management.h"
#include "test_error_handling.h"
#include "test_utilities.h"
#include "http_test_client.h"

#define TAG "TEST_HTTPD_COORDINATOR"

void setUp(){
#ifdef _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return;
    }
#endif
}

void tearDown(){
#ifdef _WIN32
    WSACleanup();
#endif
}

int test_esp_http_server(){
    return 
        test_server_lifecycle() |
        test_uri_handlers() | 
        test_utilities() | 
        test_client_management() |
        test_error_handling() | 
        test_response_handling() | 
        test_request_processing() | 
        test_websocket();
    
}

#ifdef __cplusplus
extern "C" {
#endif
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout
    setvbuf(stderr, NULL, _IONBF, 0); // Disable buffering for stderr

#if defined(__linux__)
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("Failed to set SIGPIPE handler");
        // Handle initialization failure
    }
#endif
    UNITY_BEGIN();
    test_esp_http_server();
    UNITY_END();
}

void app_main(void)
{
    main();
}
#ifdef __cplusplus
}
#endif