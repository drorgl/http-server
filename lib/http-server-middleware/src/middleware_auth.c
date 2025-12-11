#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middleware_auth.h"
#include "base64_codec.h" // Include the base64 codec library

/**
 * @brief Basic authentication middleware (enforces HTTP Basic auth)
 */
esp_err_t middleware_auth(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    const auth_config_t *config = (const auth_config_t *)ctx;

    // Skip authentication for public endpoints if configured
    if (config->allow_public && strstr(req->uri, "/public/")) {
        return ESP_OK;
    }

    // Get Authorization header
    char auth_buf[128];
    esp_err_t ret = config->req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf));
    if (ret != ESP_OK) {
        // No Authorization header - return 401 Unauthorized
        config->resp_set_status(req, "401 Unauthorized");
        config->resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }

    // Check if it's Basic auth
    if (strncmp(auth_buf, "Basic ", 6) != 0) {
        // Invalid auth type
        config->resp_set_status(req, "401 Unauthorized");
        config->resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Basic authentication required");
        return ESP_FAIL;
    }

    // Decode the base64 credentials
    char decoded[128];
    size_t decoded_len = base64_decode((const unsigned char *)(auth_buf + 6), strlen(auth_buf + 6), (unsigned char *)decoded, sizeof(decoded));
    if (decoded_len == 0) {
        // Invalid base64
        config->resp_set_status(req, "401 Unauthorized");
        config->resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials format");
        return ESP_FAIL;
    }
    decoded[decoded_len] = '\0'; // Null-terminate the decoded string

    // Parse username:password
    char *separator = strchr(decoded, ':');
    if (!separator) {

        // No colon separator
        config->resp_set_status(req, "401 Unauthorized");
        config->resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials format");
        return ESP_FAIL;
    }

    *separator = '\0';
    const char *username = decoded;
    const char *password = separator + 1;

    // Check credentials
    if (!config->username || !config->password ||
        strcmp(username, config->username) != 0 ||
        strcmp(password, config->password) != 0) {

        // Invalid credentials
        config->resp_set_status(req, "401 Unauthorized");
        config->resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
        return ESP_FAIL;
    }

    // Authentication successful
    return ESP_OK;
}
