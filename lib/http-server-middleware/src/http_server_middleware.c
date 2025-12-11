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
            if (!config->uri_match_wildcard(config->uri_pattern, req->uri, strlen(req->uri))) {
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
        fprintf(stderr, "httpd_uri_wrap_with_middleware: Failed to allocate wrapped_handler_ctx_t\\n");
        return NULL;
    }

    // Allocate configs array (shallow copy except for uri_pattern)
    ctx->configs = (httpd_middleware_config_t *)malloc(sizeof(httpd_middleware_config_t) * num_configs);
    if (ctx->configs == NULL) {
        fprintf(stderr, "httpd_uri_wrap_with_middleware: Failed to allocate middleware configs array\\n");
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
                fprintf(stderr, "httpd_uri_wrap_with_middleware: Failed to duplicate uri_pattern string\\n");
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
        fprintf(stderr, "httpd_uri_wrap_with_middleware: Failed to allocate wrapped_uri_t\\n");
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
