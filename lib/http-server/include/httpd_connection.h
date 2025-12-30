/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _HTTPD_CONNECTION_H_
#define _HTTPD_CONNECTION_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connection persistence state
 */
typedef enum {
    HTTPD_CONN_STATE_UNKNOWN = 0,    /*!< Connection state not yet determined */
    HTTPD_CONN_STATE_PERSISTENT,     /*!< Connection should be kept alive */
    HTTPD_CONN_STATE_CLOSE           /*!< Connection should be closed after response */
} httpd_conn_state_t;

/**
 * @brief Connection persistence context
 *
 * This structure tracks the persistence state and lifecycle of a single HTTP connection
 * across multiple requests, implementing RFC 9112 Section 9.3 connection persistence.
 */
typedef struct httpd_connection_ctx {
    httpd_conn_state_t state;        /*!< Current connection state */
    uint32_t request_count;          /*!< Number of requests processed on this connection */
    time_t created_at;               /*!< Timestamp when connection was established */
    time_t last_request_at;          /*!< Timestamp of last completed request */
    char connection_header[32];      /*!< Client's Connection header value (if present) */
    bool close_after_response;       /*!< Flag to force connection close after current response */
    bool is_websocket;               /*!< Flag indicating if connection has been upgraded to WebSocket */
} httpd_connection_ctx_t;

/**
 * @brief Connection persistence configuration
 *
 * Defined in http_server.h. Forward declared here for type safety.
 */
typedef struct httpd_connection_config httpd_connection_config_t;

/**
 * @brief Initialize connection persistence for a session
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return
 *  - ESP_OK : Context initialized successfully
 *  - ESP_FAIL : Failed to initialize context
 */
esp_err_t httpd_connection_init(httpd_handle_t hd, int sockfd);

/**
 * @brief Process connection headers and update persistence state
 *
 * Implements RFC 9112 Section 9.3 connection persistence algorithm:
 * - HTTP/1.1 connections are persistent by default
 * - "Connection: close" makes connection non-persistent
 * - HTTP/1.0 only persistent with explicit "keep-alive"
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @param[in] http_version HTTP version string ("HTTP/1.0" or "HTTP/1.1")
 * @param[in] connection_hdr Connection header value (NULL if not present)
 * @return
 *  - ESP_OK : Headers processed successfully
 *  - ESP_FAIL : Error processing headers
 */
esp_err_t httpd_connection_process_headers(httpd_handle_t hd, int sockfd,
                                         const char *http_version,
                                         const char *connection_hdr);

/**
 * @brief Check if connection should remain persistent
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return true if connection should remain open, false if should be closed
 */
bool httpd_connection_should_persist(httpd_handle_t hd, int sockfd);

/**
 * @brief Mark connection for closure after current response
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 */
void httpd_connection_close_after_response(httpd_handle_t hd, int sockfd);

/**
 * @brief Check if connection should be closed after current response
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return true if connection should be closed after response
 */
bool httpd_connection_should_close_after_response(httpd_handle_t hd, int sockfd);

/**
 * @brief Increment request counter for connection
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return
 *  - ESP_OK : Counter incremented successfully
 *  - ESP_FAIL : Error updating counter
 */
esp_err_t httpd_connection_increment_request_count(httpd_handle_t hd, int sockfd);

/**
 * @brief Update last request timestamp for connection
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 */
void httpd_connection_update_timestamp(httpd_handle_t hd, int sockfd);

/**
 * @brief Check if connection has exceeded limits
 *
 * Checks maximum requests per connection and age limits.
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return true if connection has exceeded limits and should be closed
 */
bool httpd_connection_exceeded_limits(httpd_handle_t hd, int sockfd);

/**
 * @brief Mark connection as upgraded to WebSocket
 *
 * WebSocket connections have different persistence rules and should not
 * be subject to HTTP connection limits.
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 */
void httpd_connection_mark_websocket(httpd_handle_t hd, int sockfd);

/**
 * @brief Get current connection context
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 * @return pointer to connection context or NULL if not found
 */
httpd_connection_ctx_t* httpd_connection_get_ctx(httpd_handle_t hd, int sockfd);

/**
 * @brief Clean up connection context for session
 *
 * @param[in] hd        HTTP server handle
 * @param[in] sockfd    Session socket descriptor
 */
void httpd_connection_cleanup(httpd_handle_t hd, int sockfd);

#ifdef __cplusplus
}
#endif

#endif /* _HTTPD_CONNECTION_H_ */
