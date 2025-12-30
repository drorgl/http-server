/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include <esp_err.h>
#endif

#include "httpd_connection.h"
#include "esp_httpd_priv.h"
#include <log.h>
#include <ctype.h>

#ifndef isspace
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\f' || (c) == '\v')
#endif

static const char *TAG = "httpd_conn";

/**
 * Trim leading and trailing whitespace from a string
 * @param str Input string (modified in-place)
 * @return Pointer to trimmed string (within original buffer)
 */
static char* trim_whitespace(char *str) {
    if (!str || !*str) {
        return str;
    }

    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end >= start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return start;
}

/**
 * Free function for connection context
 */
static void httpd_connection_ctx_free(void *ctx)
{
    if (ctx) {
        free(ctx);
    }
}

esp_err_t httpd_connection_init(httpd_handle_t hd, int sockfd)
{
    if (!hd || sockfd < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get session
    struct sock_db *session = httpd_sess_get(hd, sockfd);
    if (!session) {
        LOGE(TAG, "No session found for fd=%d", sockfd);
        return ESP_FAIL;
    }

    // Allocate connection context
    httpd_connection_ctx_t *ctx = calloc(1, sizeof(httpd_connection_ctx_t));
    if (!ctx) {
        LOGE(TAG, "Failed to allocate connection context");
        return ESP_FAIL;
    }

    // Initialize context
    ctx->state = HTTPD_CONN_STATE_UNKNOWN;
    ctx->request_count = 0;
    ctx->created_at = time(NULL);
    ctx->last_request_at = ctx->created_at;
    ctx->close_after_response = false;
    ctx->is_websocket = false;
    memset(ctx->connection_header, 0, sizeof(ctx->connection_header));

    // Store context in session
    session->connection_ctx = ctx;
    session->free_connection_ctx = httpd_connection_ctx_free;

    LOGD(TAG, "Initialized connection context for fd=%d", sockfd);
    return ESP_OK;
}

esp_err_t httpd_connection_process_headers(httpd_handle_t hd, int sockfd,
                                         const char *http_version,
                                         const char *connection_hdr)
{
    if (!hd || sockfd < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (!ctx) {
        LOGE(TAG, "No connection context for fd=%d", sockfd);
        return ESP_FAIL;
    }

    // FIX: WebSocket connections maintain their persistence state
    // Skip HTTP header processing for WebSocket frames (no headers present)
    if (ctx->is_websocket) {
        LOGD(TAG, "Skipping header processing for WebSocket connection fd=%d", sockfd);
        return ESP_OK;
    }

    // RFC 9112 Section 9.3 Connection Persistence Algorithm
    bool should_close = false;

    // Debug logging for header processing
    LOGD(TAG, "Connection processing: version='%s', hdr='%s'",
         http_version ? http_version : "NULL",
         connection_hdr ? connection_hdr : "NULL");

    // Trim whitespace from connection header for proper comparison
    char trimmed_connection[64] = {0}; // Buffer for trimmed header
    const char *effective_connection_hdr = NULL;

    if (connection_hdr) {
        strncpy(trimmed_connection, connection_hdr, sizeof(trimmed_connection) - 1);
        effective_connection_hdr = trim_whitespace(trimmed_connection);
        LOGD(TAG, "Trimmed connection header: '%s' -> '%s'",
             connection_hdr, effective_connection_hdr);
    }

    // Check if "close" connection option is present (case-insensitive)
    if (effective_connection_hdr && strcasecmp(effective_connection_hdr, "close") == 0) {
        should_close = true;
        strncpy(ctx->connection_header, "close", sizeof(ctx->connection_header) - 1);
        LOGD(TAG, "Connection: close directive found");
    } else if (connection_hdr) {
        // Store the original connection header value for reference (untrimmed)
        // but use trimmed version for comparison
        strncpy(ctx->connection_header, connection_hdr, sizeof(ctx->connection_header) - 1);
        LOGD(TAG, "Connection header: %s", connection_hdr);
    }

    // Apply RFC 9112 persistence rules
    if (should_close) {
        // Rule 1: "close" connection option present
        ctx->state = HTTPD_CONN_STATE_CLOSE;
    } else if (http_version && strcmp(http_version, "HTTP/1.1") == 0) {
        // Rule 2: HTTP/1.1 defaults to persistent
        ctx->state = HTTPD_CONN_STATE_PERSISTENT;
        LOGD(TAG, "HTTP/1.1 connection marked as persistent");
    } else if (http_version && strcmp(http_version, "HTTP/1.0") == 0 &&
               effective_connection_hdr && strcasecmp(effective_connection_hdr, "keep-alive") == 0) {
        // Rule 3: HTTP/1.0 with explicit keep-alive (with proxy restrictions)
        // Note: We implement basic keep-alive support for HTTP/1.0
        // Full RFC 9112 compliance would require proxy awareness
        ctx->state = HTTPD_CONN_STATE_PERSISTENT;
        LOGD(TAG, "HTTP/1.0 keep-alive connection marked as persistent");
    } else {
        // Rule 4: Default to close for HTTP/1.0 without keep-alive
        ctx->state = HTTPD_CONN_STATE_CLOSE;
        LOGD(TAG, "Connection marked for closure (default behavior)");
    }

    // Increment request count
    ctx->request_count++;
    ctx->last_request_at = time(NULL);

    return ESP_OK;
}

bool httpd_connection_should_persist(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (!ctx) {
        LOGW(TAG, "No connection context for fd=%d in should_persist, returning false", sockfd);
        return false; // No context means we don't know, default to close
    }

    // Connections marked for close should not persist
    if (ctx->state == HTTPD_CONN_STATE_CLOSE) {
        return false;
    }

    // Check if force close is requested
    if (ctx->close_after_response) {
        return false;
    }

    // Check if connection has exceeded limits (future enhancement)
    if (httpd_connection_exceeded_limits(hd, sockfd)) {
        return false;
    }

    // WebSocket connections have different persistence rules
    if (ctx->is_websocket) {
        return true; // WebSocket connections persist until explicitly closed
    }

    return (ctx->state == HTTPD_CONN_STATE_PERSISTENT);
}

void httpd_connection_close_after_response(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (ctx) {
        ctx->close_after_response = true;
        LOGD(TAG, "Connection fd=%d marked for closure after response", sockfd);
    }
}

bool httpd_connection_should_close_after_response(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    return ctx ? ctx->close_after_response : false;
}

esp_err_t httpd_connection_increment_request_count(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (!ctx) {
        return ESP_FAIL;
    }

    ctx->request_count++;
    ctx->last_request_at = time(NULL);
    LOGD(TAG, "Request count for fd=%d now %u", sockfd, ctx->request_count);
    return ESP_OK;
}

void httpd_connection_update_timestamp(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (ctx) {
        ctx->last_request_at = time(NULL);
    }
}

bool httpd_connection_exceeded_limits(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (!ctx) {
        return false;
    }

    // WebSocket connections don't have HTTP-level limits
    if (ctx->is_websocket) {
        return false;
    }

    // Get server configuration
    struct sock_db *session = httpd_sess_get(hd, sockfd);
    if (!session || !session->handle) {
        return false;
    }
    struct httpd_data *hd_data = (struct httpd_data *)session->handle;

    // Check maximum requests per connection limit
    if (hd_data->config.connection_config.max_requests_per_conn > 0 &&
        ctx->request_count > hd_data->config.connection_config.max_requests_per_conn) {
        LOGD(TAG, "Connection fd=%d exceeded max requests per connection (%u > %u)",
             sockfd, ctx->request_count, hd_data->config.connection_config.max_requests_per_conn);
        return true;
    }

    // Check maximum idle timeout
    if (hd_data->config.connection_config.max_idle_sec > 0) {
        time_t now = time(NULL);
        time_t idle_time = now - ctx->last_request_at;
        if (idle_time >= hd_data->config.connection_config.max_idle_sec) {
            LOGD(TAG, "Connection fd=%d exceeded max idle timeout (%ld >= %u seconds)",
                 sockfd, idle_time, hd_data->config.connection_config.max_idle_sec);
            return true;
        }
    }

    // Check maximum connection lifetime
    if (hd_data->config.connection_config.max_lifetime_sec > 0) {
        time_t now = time(NULL);
        time_t lifetime = now - ctx->created_at;
        if (lifetime >= hd_data->config.connection_config.max_lifetime_sec) {
            LOGD(TAG, "Connection fd=%d exceeded max lifetime (%ld >= %u seconds)",
                 sockfd, lifetime, hd_data->config.connection_config.max_lifetime_sec);
            return true;
        }
    }

    return false; // No limits exceeded
}

void httpd_connection_mark_websocket(httpd_handle_t hd, int sockfd)
{
    httpd_connection_ctx_t *ctx = httpd_connection_get_ctx(hd, sockfd);
    if (ctx) {
        ctx->is_websocket = true;
        LOGD(TAG, "Connection fd=%d marked as WebSocket", sockfd);
    }
}

httpd_connection_ctx_t* httpd_connection_get_ctx(httpd_handle_t hd, int sockfd)
{
    struct sock_db *session = httpd_sess_get(hd, sockfd);
    return session ? session->connection_ctx : NULL;
}

void httpd_connection_cleanup(httpd_handle_t hd, int sockfd)
{
    // The context will be freed automatically by httpd_sess_free_ctx
    // when the session is cleaned up, due to our httpd_connection_ctx_free function
    LOGD(TAG, "Cleaning up connection context for fd=%d", sockfd);
}
