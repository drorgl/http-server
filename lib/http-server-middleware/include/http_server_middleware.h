#ifndef _HTTP_SERVER_MIDDLEWARE_H_
#define _HTTP_SERVER_MIDDLEWARE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Middleware function type
 *
 * @param req HTTP request structure
 * @param uri Matching URI handler (NULL if no match found yet)
 * @param ctx User context passed during registration
 *
 * @return ESP_OK to continue processing, error code to short-circuit
 */
typedef esp_err_t (*httpd_middleware_func_t)(httpd_req_t *req,
                                           const httpd_uri_t *uri,
                                           void *ctx);

/**
 * @brief Prototype for freeing context data (if any)
 * @param[in] ctx   object to free
 */
typedef void (*httpd_middleware_free_ctx_fn_t)(void *ctx);

/**
 * @brief Middleware configuration structure
 */
typedef struct httpd_middleware_config {
    httpd_middleware_func_t func;                          /**< Middleware function */
    void *context;                                         /**< User context for middleware */
    httpd_middleware_free_ctx_fn_t free_ctx;               /**< Context cleanup function */
    int priority;                                          /**< Deprecated - execution order determined by array position */
    const char *uri_pattern;                               /**< URI pattern filter (exact match for POC, wildcards later) */
    httpd_method_t method_filter;                          /**< HTTP method filter (HTTP_ANY for all) */
    bool enabled;                                          /**< Enable/disable flag */
    // Function callback for dependency injection
    bool (*uri_match_wildcard)(const char *uri_template, const char *uri, size_t match_upto); /**< URI wildcard matching callback */
} httpd_middleware_config_t;

/**
 * @brief Context structure for wrapped handlers (exposed for testing)
 */
typedef struct wrapped_handler_ctx {
    const httpd_uri_t *original_uri;           /**< Original URI handler */
    httpd_middleware_config_t *configs;        /**< Array of middleware configs */
    size_t num_configs;                        /**< Number of configs */
} wrapped_handler_ctx_t;

/**
 * @brief Wrap a URI handler with middleware chain
 *
 * This function creates a new URI handler where the middleware chain is executed
 * before the original handler. Middleware can short-circuit the request by returning
 * an error code, or allow processing to continue by returning ESP_OK.
 *
 * @param original_uri Original URI handler to wrap
 * @param configs Array of middleware configurations (executed in order provided)
 * @param num_configs Number of middleware configurations
 *
 * @return New httpd_uri_t with wrapped handler, or NULL on error
 * @note The returned httpd_uri_t must be freed by the caller when no longer needed
 */
httpd_uri_t* httpd_uri_wrap_with_middleware(const httpd_uri_t *original_uri,
                                           const httpd_middleware_config_t *configs,
                                           size_t num_configs);

#ifdef __cplusplus
}
#endif

#endif /* _HTTP_SERVER_MIDDLEWARE_H_ */
