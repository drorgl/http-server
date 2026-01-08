#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middleware_auth.h"
#include "middleware_strings.h"
#include "base64_codec.h" // Include the base64 codec library

/**
 * @brief Basic authentication middleware using configurable callbacks
 *
 * First checks if auth is required for the URI via requires_auth callback.
 * If required, validates Basic Auth credentials via check_credentials callback.
 */
esp_err_t middleware_auth(httpd_req_t *req, const httpd_uri_t *uri, void *ctx)
{
    const auth_config_t *config = (const auth_config_t *)ctx;

    // Check if this URI requires authentication
    if (config->requires_auth && !config->requires_auth(req->uri, config->bypass_ctx)) {
        return ESP_OK;  // Bypass auth for this path
    }

    // Get Authorization header
    char auth_buf[128];
    esp_err_t ret = config->req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf));
    if (ret != ESP_OK) {
        // No Authorization header - return 401 Unauthorized
        config->resp_set_status(req, HTTP_STATUS_401_UNAUTHORIZED);
        config->resp_set_hdr(req, HTTP_HDR_WWW_AUTHENTICATE, HTTP_AUTH_BASIC_REALM);
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, HTTP_ERR_AUTH_REQUIRED);
        return ESP_FAIL;
    }

    // Check if it's Basic auth
    if (strncmp(auth_buf, "Basic ", 6) != 0) {
        // Invalid auth type
        config->resp_set_status(req, HTTP_STATUS_401_UNAUTHORIZED);
        config->resp_set_hdr(req, HTTP_HDR_WWW_AUTHENTICATE, HTTP_AUTH_BASIC_REALM);
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, HTTP_ERR_AUTH_BASIC_REQUIRED);
        return ESP_FAIL;
    }

    // Decode the base64 credentials
    char decoded[128];
    size_t decoded_len = base64_decode((const char *)(auth_buf + 6), strlen(auth_buf + 6), (unsigned char *)decoded, sizeof(decoded));
    if (decoded_len == 0) {
        // Invalid base64
        config->resp_set_status(req, HTTP_STATUS_401_UNAUTHORIZED);
        config->resp_set_hdr(req, HTTP_HDR_WWW_AUTHENTICATE, HTTP_AUTH_BASIC_REALM);
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, HTTP_ERR_AUTH_INVALID_FORMAT);
        return ESP_FAIL;
    }
    decoded[decoded_len] = '\0'; // Null-terminate the decoded string

    // Parse username:password
    char *separator = strchr(decoded, ':');
    if (!separator) {

        // No colon separator
        config->resp_set_status(req, HTTP_STATUS_401_UNAUTHORIZED);
        config->resp_set_hdr(req, HTTP_HDR_WWW_AUTHENTICATE, HTTP_AUTH_BASIC_REALM);
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, HTTP_ERR_AUTH_INVALID_FORMAT);
        return ESP_FAIL;
    }

    *separator = '\0';
    const char *username = decoded;
    const char *password = separator + 1;

    // Check credentials via callback
    if (!config->check_credentials || config->check_credentials(username, password, config->check_ctx) != ESP_OK) {
        // Invalid credentials
        config->resp_set_status(req, HTTP_STATUS_401_UNAUTHORIZED);
        config->resp_set_hdr(req, HTTP_HDR_WWW_AUTHENTICATE, HTTP_AUTH_BASIC_REALM);
        config->resp_send_err(req, HTTPD_401_UNAUTHORIZED, HTTP_ERR_AUTH_INVALID_CREDS);
        return ESP_FAIL;
    }

    // Authentication successful
    return ESP_OK;
}
