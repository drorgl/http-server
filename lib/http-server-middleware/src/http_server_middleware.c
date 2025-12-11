#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/http_server_middleware.h"

/**
 * @brief Context structure for wrapped handlers
 */
typedef struct wrapped_handler_ctx {
    const httpd_uri_t *original_uri;           /**< Original URI handler */
    httpd_middleware_config_t *configs;        /**< Array of middleware configs */
    size_t num_configs;                        /**< Number of configs */
} wrapped_handler_ctx_t;

/**
 * @brief Wrapped handler function that executes middleware chain
 */
static esp_err_t wrapped_handler(httpd_req_t *req)
{
    // Get our context from the user_ctx
    wrapped_handler_ctx_t *ctx = (wrapped_handler_ctx_t *)req->user_ctx;

    for (size_t i = 0; i < ctx->num_configs; i++) {
        httpd_middleware_config_t *config = &ctx->configs[i];

        // Skip disabled middleware
        if (!config->enabled) {
            continue;
        }

        // URI pattern wildcard matching
        if (config->uri_pattern != NULL) {
            if (!httpd_uri_match_wildcard(config->uri_pattern, req->uri, strlen(req->uri))) {
                continue;  // URI doesn't match, skip this middleware
            }
        }

        // HTTP method filtering
        if (config->method_filter != HTTP_ANY &&
            config->method_filter != req->method) {
            continue;  // Method doesn't match, skip this middleware
        }

        // Execute middleware
        esp_err_t ret = config->func(req, ctx->original_uri, config->context);
        if (ret != ESP_OK) {
            // Middleware short-circuited the request
            return ret;
        }
    }

    // All middleware passed, execute original handler
    return ctx->original_uri->handler(req);
}

/**
 * @brief Free function for wrapped handler context
 */
static void free_wrapped_ctx(void *ctx)
{
    wrapped_handler_ctx_t *wrapped_ctx = (wrapped_handler_ctx_t *)ctx;

    // Free the configs array (with properly duplicated strings)
    if (wrapped_ctx->configs) {
        for (size_t i = 0; i < wrapped_ctx->num_configs; i++) {
            // Free any duplicated uri_pattern strings
            if (wrapped_ctx->configs[i].uri_pattern) {
                free((char *)wrapped_ctx->configs[i].uri_pattern);
            }
            // Note: Other fields (context) are managed by caller via free_ctx function
        }
        free(wrapped_ctx->configs);
    }

    free(wrapped_ctx);
}

httpd_uri_t* httpd_uri_wrap_with_middleware(const httpd_uri_t *original_uri,
                                           const httpd_middleware_config_t *configs,
                                           size_t num_configs)
{
    if (original_uri == NULL || configs == NULL || num_configs == 0) {
        return NULL;
    }

    // Allocate context for wrapped handler
    wrapped_handler_ctx_t *ctx = (wrapped_handler_ctx_t *)malloc(sizeof(wrapped_handler_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    // Allocate configs array (shallow copy except for uri_pattern)
    ctx->configs = (httpd_middleware_config_t *)malloc(sizeof(httpd_middleware_config_t) * num_configs);
    if (ctx->configs == NULL) {
        free(ctx);
        return NULL;
    }

    // Copy configs (shallow copy, but duplicate uri_pattern strings)
    for (size_t i = 0; i < num_configs; i++) {
        ctx->configs[i] = configs[i];
        // Duplicate uri_pattern strings for proper lifecycle management
        if (configs[i].uri_pattern) {
            ctx->configs[i].uri_pattern = strdup(configs[i].uri_pattern);
            if (ctx->configs[i].uri_pattern == NULL) {
                // Memory allocation failed, cleanup and return NULL
                // Free any previously allocated strings
                for (size_t j = 0; j < i; j++) {
                    if (ctx->configs[j].uri_pattern) {
                        free((char *)ctx->configs[j].uri_pattern);
                    }
                }
                free(ctx->configs);
                free(ctx);
                return NULL;
            }
        }
    }

    ctx->original_uri = original_uri;
    ctx->num_configs = num_configs;

    // Allocate new URI handler
    httpd_uri_t *wrapped_uri = (httpd_uri_t *)malloc(sizeof(httpd_uri_t));
    if (wrapped_uri == NULL) {
        free(ctx->configs);
        free(ctx);
        return NULL;
    }

    // Copy original URI fields
    wrapped_uri->uri = original_uri->uri;  // Assume managed by caller
    wrapped_uri->method = original_uri->method;
    wrapped_uri->user_ctx = ctx;           // Our wrapped context
    wrapped_uri->handler = wrapped_handler; // Our wrapped handler function

#ifdef CONFIG_HTTPD_WS_SUPPORT
    wrapped_uri->is_websocket = original_uri->is_websocket;
    wrapped_uri->handle_ws_control_frames = original_uri->handle_ws_control_frames;
    wrapped_uri->supported_subprotocol = original_uri->supported_subprotocol;
#endif

    return wrapped_uri;
}

/*
 * Simple example middleware implementations for POC
 */

/**
 * @brief Basic logging middleware
 */
esp_err_t middleware_logging(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    printf("Middleware LOG: %s %s\n", http_method_str(req->method), req->uri);
    return ESP_OK;
}

/**
 * @brief Helper function to check if origin is in allowed list
 */
static bool cors_origin_allowed(const char *origin, const char *allowed_origins)
{
    if (strcmp(allowed_origins, "*") == 0) {
        return true;
    }

    // Simple comma-separated list check (could be optimized)
    const char *ptr = allowed_origins;
    while (*ptr) {
        const char *start = ptr;
        while (*ptr && *ptr != ',') {
            ptr++;
        }

        size_t len = ptr - start;
        if (strncmp(origin, start, len) == 0 && strlen(origin) == len) {
            return true;
        }

        if (*ptr == ',') ptr++; // Skip comma and space
        while (*ptr == ' ') ptr++; // Skip spaces after comma
    }

    return false;
}

/**
 * @brief CORS middleware implementation
 *
 * Handles preflight OPTIONS requests and adds CORS headers to responses
 */
esp_err_t middleware_cors(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    const cors_config_t *config = (const cors_config_t *)ctx;

    // Get Origin header from request
    char origin_header[128];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin_header, sizeof(origin_header)) != ESP_OK) {
        // No Origin header, not a CORS request - continue processing
        return ESP_OK;
    }

    // Check if origin is allowed
    if (!cors_origin_allowed(origin_header, config->allowed_origins)) {
        printf("CORS: Origin '%s' not allowed\n", origin_header);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Origin not allowed");
        return ESP_FAIL;
    }

    // Handle preflight OPTIONS request
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_set_status(req, HTTPD_200);

        // Set CORS headers for preflight
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin_header);
        if (config->allowed_methods) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", config->allowed_methods);
        }
        if (config->allowed_headers) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", config->allowed_headers);
        }
        if (config->allow_credentials) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
        }
        if (config->max_age > 0) {
            char max_age_str[16];
            sprintf(max_age_str, "%d", config->max_age);
            httpd_resp_set_hdr(req, "Access-Control-Max-Age", max_age_str);
        }

        httpd_resp_send(req, NULL, 0);
        printf("CORS: Preflight OPTIONS handled\n");
        return ESP_OK; // Handled OPTIONS request, return without calling handler
    }

    // For non-OPTIONS requests, just add the basic CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin_header);
    if (config->allow_credentials) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
    }

    printf("CORS: Added headers for %s from %s\n", req->uri, origin_header);
    return ESP_OK; // Continue to execute original handler
}

/**
 * @brief Basic authentication middleware (checks for Authorization header)
 */
esp_err_t middleware_auth(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    // Skip auth for public endpoints
    if (strstr(req->uri, "/public/")) {
        return ESP_OK;
    }

    // Check for Authorization header (simplified)
    char auth_buf[128];
    esp_err_t ret = httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf));
    if (ret != ESP_OK) {
        // In real implementation, would return 401
        printf("Middleware AUTH: Missing Authorization header\n");
        // For POC, just log and continue
    } else {
        printf("Middleware AUTH: Found Authorization: %s\n", auth_buf);
    }

    return ESP_OK;
}
