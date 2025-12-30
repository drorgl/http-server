/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* HTTP Server Connection Persistence Example

   This example demonstrates RFC 9112 HTTP/1.1 connection persistence features:
   - HTTP/1.1 connections remain persistent by default
   - Connection: close header enforcement for connection termination
   - HTTP/1.0 behavior (close by default, keep-alive when requested)
   - Connection state monitoring and reporting
   - Configurable connection limits and timeouts

   Features demonstrated:
   - /status: Shows connection stats (request count, state, persistence)
   - /close: Forces connection termination with Connection: close
   - /multiple: Accepts multiple requests on same connection to show persistence
   - /version: Demonstrates HTTP/1.0 vs HTTP/1.1 differences
*/

#include <esp_log.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <stdlib.h>
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include <httpd_connection.h>

static const char *TAG = "connection_persistence_example";

/* Context to track connection request counts for /multiple endpoint */
typedef struct {
    int request_count;
} connection_context_t;

/* Helper to clean up connection context */
void connection_context_free(void *ctx)
{
    free(ctx);
}

/* Handler for /status endpoint - shows connection state information */
static esp_err_t status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /status request");

    // Get connection state information
    bool will_persist = httpd_connection_should_persist(req->handle, httpd_req_to_sockfd(req));

    // Get connection context
    httpd_connection_ctx_t* conn_ctx = httpd_connection_get_ctx(req->handle, httpd_req_to_sockfd(req));

    // Generate JSON response with connection details
    char response[512];
    int len = snprintf(response, sizeof(response),
        "{"
        "\"connection_state\": \"%s\","
        "\"will_persist\": %s,"
        "\"request_count\": %d,"
        "\"connection_close_after_response\": %s,"
        "\"is_websocket\": %s,"
        "\"version\": \"HTTP/1.%d\","
        "\"sockfd\": %d"
        "}\n",
        conn_ctx ? (conn_ctx->state == HTTPD_CONN_STATE_PERSISTENT ? "persistent" : "close") : "unknown",
        will_persist ? "true" : "false",
        conn_ctx ? conn_ctx->request_count : 0,
        conn_ctx && conn_ctx->close_after_response ? "true" : "false",
        conn_ctx && conn_ctx->is_websocket ? "true" : "false",
        req->version && strstr(req->version, "1.0") ? 0 : 1,
        httpd_req_to_sockfd(req));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);

    return ESP_OK;
}

/* Handler for /close endpoint - forces connection termination */
static esp_err_t close_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /close request - will force connection termination");

    httpd_resp_send(req, "Connection will be closed after this response\n", HTTPD_RESP_USE_STRLEN);

    // Force the connection to close after this response
    httpd_connection_close_after_response(req->handle, httpd_req_to_sockfd(req));

    return ESP_OK;
}

/* Handler for /multiple endpoint - tracks requests on same connection */
static esp_err_t multiple_handler(httpd_req_t *req)
{
    // Get or create connection context
    connection_context_t *ctx = req->sess_ctx;
    if (!ctx) {
        ctx = calloc(1, sizeof(connection_context_t));
        if (!ctx) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_OK;
        }
        req->sess_ctx = ctx;
        req->free_ctx = connection_context_free;
    }

    ctx->request_count++;

    char response[128];
    int len = snprintf(response, sizeof(response),
        "Request #%d on this connection\n"
        "Connection will persist for more requests\n",
        ctx->request_count);

    httpd_resp_send(req, response, len);

    return ESP_OK;
}

/* Handler for /version endpoint - demonstrates HTTP version differences */
static esp_err_t version_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /version request");

    char response[256];
    const char *version = req->version ? req->version : "unknown";
    bool is_http10 = (strcmp(version, "HTTP/1.0") == 0);
    bool will_persist = httpd_connection_should_persist(req->handle, httpd_req_to_sockfd(req));

    int len = snprintf(response, sizeof(response),
        "HTTP Version: %s\n"
        "Default Connection Behavior: %s\n"
        "This connection will persist: %s\n"
        "\n"
        "HTTP/1.1: Persistent by default\n"
        "HTTP/1.0: Close by default, persistent with 'Connection: keep-alive'\n",
        version,
        is_http10 ? "CLOSE" : "PERSISTENT",
        will_persist ? "YES" : "NO");

    httpd_resp_send(req, response, len);

    return ESP_OK;
}

/* Handler for /limit endpoint - demonstrates connection limit enforcement */
static esp_err_t limit_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /limit request");

    // Get current config to show limits
    httpd_config_t config = {
        .connection_config.max_requests_per_conn = 5,
        .connection_config.max_idle_sec = 30,
        .connection_config.max_lifetime_sec = 300
    };

    char response[256];
    int len = snprintf(response, sizeof(response),
        "Connection Limits:\n"
        "- Max requests per connection: %u\n"
        "- Max idle time (seconds): %u\n"
        "- Max lifetime (seconds): %u\n"
        "\n"
        "This server uses custom connection limits.\n"
        "Try making more than 5 requests to see limit enforcement.\n",
        config.connection_config.max_requests_per_conn,
        config.connection_config.max_idle_sec,
        config.connection_config.max_lifetime_sec);

    httpd_resp_send(req, response, len);

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Configure server with connection persistence settings
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Custom connection persistence configuration
    config.connection_config.enable_persistence = true;          // Enable connection persistence
    config.connection_config.max_requests_per_conn = 5;          // Max 5 requests per connection
    config.connection_config.max_idle_sec = 30;                  // 30 second idle timeout
    config.connection_config.max_lifetime_sec = 300;             // 5 minute max lifetime

    // Server configuration
    config.server_port = 8080;
    config.lru_purge_enable = true;

#if CONFIG_IDF_TARGET_LINUX
    config.server_port = 8080;  // Use alternative port for Linux
#endif

    ESP_LOGI(TAG, "Starting server with connection persistence on port: %d", config.server_port);
    ESP_LOGI(TAG, "Connection config: max_req=%u, idle_timeout=%u, lifetime=%u",
             config.connection_config.max_requests_per_conn,
             config.connection_config.max_idle_sec,
             config.connection_config.max_lifetime_sec);

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        ESP_LOGI(TAG, "Registering connection persistence demo handlers");

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t close_uri = {
            .uri = "/close",
            .method = HTTP_GET,
            .handler = close_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &close_uri);

        httpd_uri_t multiple_uri = {
            .uri = "/multiple",
            .method = HTTP_GET,
            .handler = multiple_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &multiple_uri);

        httpd_uri_t version_uri = {
            .uri = "/version",
            .method = HTTP_GET,
            .handler = version_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &version_uri);

        httpd_uri_t limit_uri = {
            .uri = "/limit",
            .method = HTTP_GET,
            .handler = limit_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &limit_uri);

        ESP_LOGI(TAG, "Connection persistence handlers registered:");
        ESP_LOGI(TAG, "  GET /status   - Connection state information");
        ESP_LOGI(TAG, "  GET /close    - Force connection termination");
        ESP_LOGI(TAG, "  GET /multiple - Track requests on same connection");
        ESP_LOGI(TAG, "  GET /version  - HTTP version behavior");
        ESP_LOGI(TAG, "  GET /limit    - Connection limits info");

        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX

void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "=== HTTP Server Connection Persistence Example ===");
    ESP_LOGI(TAG, "This example demonstrates RFC 9112 connection persistence features");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Configure Wi-Fi or Ethernet */
    ESP_ERROR_CHECK(example_connect());

#if !CONFIG_IDF_TARGET_LINUX
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET
#endif // !CONFIG_IDF_TARGET_LINUX

    /* Start the server */
    server = start_webserver();

    ESP_LOGI(TAG, "Example started. Use curl or browser to test endpoints:");
    ESP_LOGI(TAG, "  curl -v http://<IP>:8080/status");
    ESP_LOGI(TAG, "  curl -v http://<IP>:8080/multiple (try multiple times)");
    ESP_LOGI(TAG, "  curl -v http://<IP>:8080/close");
    ESP_LOGI(TAG, "  curl -v 'http://<IP>:8080/version' -H 'Connection: close'");

    /* Keep server running */
    while (server) {
        ESP_LOGI(TAG, "Server running... (stats available at /status)");
#ifdef CONFIG_IDF_TARGET_LINUX
        sleep(30);  // Print status every 30 seconds on Linux
#else
        vTaskDelay(pdMS_TO_TICKS(30000));  // Print status every 30 seconds
#endif
    }
}
