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
    const char *username;            /**< Expected username for authentication */
    const char *password;            /**< Expected password for authentication */
    bool allow_public;               /**< Allow access to /public/ endpoints without auth */
    // Function callbacks for dependency injection
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size); /**< Get header value callback */
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);                                 /**< Set response status callback */
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);                  /**< Set response header callback */
    esp_err_t (*resp_send_err)(httpd_req_t *req, httpd_err_code_t error, const char *message);          /**< Send error response callback */
} auth_config_t;

/**
 * @brief Basic authentication middleware
 *
 * Checks for Authorization header on non-public endpoints.
 * Public endpoints (paths starting with "/public/") skip authentication.
 */
esp_err_t middleware_auth(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_AUTH_H_ */
