#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middleware_logging.h"

/**
 * @brief Basic logging middleware
 */
esp_err_t middleware_logging(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    const logging_config_t *config = (const logging_config_t *)ctx;

    // Log request details based on log level
    if (config->log_level >= 1) {
        config->printf("Middleware LOG: %s %s\n",
               config->method_str(req->method), req->uri);

        if (config->log_level >= 2) {
            // Log headers for higher verbosity
            // (In a full implementation, we'd iterate through all headers)
            char user_agent[128];
            if (config->req_get_hdr_value_str(req, "User-Agent", user_agent, sizeof(user_agent)) == ESP_OK) {
                config->printf("  User-Agent: %s\n", user_agent);
            }

            if (config->log_level >= 3) {
                config->printf("  Content-Length: %d\n", req->content_len);
            }
        }
    }

    return ESP_OK;
}
