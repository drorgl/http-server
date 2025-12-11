#ifndef _MIDDLEWARE_AUTH_H_
#define _MIDDLEWARE_AUTH_H_

#include <stdbool.h>
#include <stddef.h>

#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Authentication configuration structure
 */
typedef struct auth_config {
    esp_err_t (*check_credentials)(const char *username, const char *password, void *check_ctx);  /**< Callback to validate parsed credentials */
    void *check_ctx;                                                                             /**< Context for credential callback */
    
    bool (*requires_auth)(const char *uri, void *bypass_ctx);                                     /**< Callback to check if URI requires auth (true=auth needed) */
    void *bypass_ctx;                                                                            /**< Context for bypass callback */
    
    // Function callbacks for dependency injection
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size); /**< Get header value callback */
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);                                 /**< Set response status callback */
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);                  /**< Set response header callback */
    esp_err_t (*resp_send_err)(httpd_req_t *req, httpd_err_code_t error, const char *message);          /**< Send error response callback */
} auth_config_t;

/**
 * @brief Basic authentication middleware (callback-based)
 *
 * 1. Calls requires_auth(uri, bypass_ctx): if false, bypasses auth entirely (ESP_OK).
 * 2. If auth required: Parses Basic Auth header, validates format, calls check_credentials(username, password, check_ctx).
 * 3. Returns ESP_OK on success, ESP_FAIL + 401 on failure.
 *
 * NULL callbacks: requires_auth=NULL requires auth always; check_credentials=NULL denies always.
 */
esp_err_t middleware_auth(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_AUTH_H_ */
