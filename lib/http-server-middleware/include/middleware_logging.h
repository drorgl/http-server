#ifndef _MIDDLEWARE_LOGGING_H_
#define _MIDDLEWARE_LOGGING_H_

#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logging configuration structure
 */
typedef struct logging_config {
    int log_level;                   /**< Minimum log level (0-3, higher is more verbose) */
    // Function callbacks for dependency injection
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size); /**< Get header value callback */
    const char* (*method_str)(httpd_method_t method);                                                   /**< Convert method to string callback */
    int (*printf)(const char *format, ...);                                                             /**< Logging printf callback */
} logging_config_t;

/**
 * @brief Logging middleware - logs request details
 */
esp_err_t middleware_logging(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_LOGGING_H_ */
