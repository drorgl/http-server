#ifndef _MIDDLEWARE_CORS_H_
#define _MIDDLEWARE_CORS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <http_server.h>  // For httpd_req_t and related types

/*
 * Define ssize_t if not available
 */
#ifndef ssize_t
typedef intptr_t ssize_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CORS configuration structure
 */
typedef struct cors_config {
    const char *allowed_origins;     /**< Comma-separated list of allowed origins, or "*" for all */
    const char *allowed_methods;     /**< Comma-separated list of allowed methods */
    const char *allowed_headers;     /**< Comma-separated list of allowed headers */
    bool allow_credentials;          /**< Whether to allow credentials */
    int max_age;                     /**< Max age for preflight cache in seconds */
    // Function callbacks for dependency injection
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size); /**< Get header value callback */
    esp_err_t (*resp_send_err)(httpd_req_t *req, httpd_err_code_t error, const char *message);          /**< Send error response callback */
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);                                 /**< Set response status callback */
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);                  /**< Set response header callback */
    esp_err_t (*resp_send)(httpd_req_t *req, const char *buf, ssize_t buf_len);                         /**< Send response callback */
} cors_config_t;

/**
 * @brief CORS middleware - handles Cross-Origin Resource Sharing
 *
 * This middleware adds CORS headers to responses and handles preflight OPTIONS requests.
 * It can be configured with different origins, methods, and headers.
 */
esp_err_t middleware_cors(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_CORS_H_ */
