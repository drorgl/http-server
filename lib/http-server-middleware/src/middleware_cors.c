#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middleware_cors.h"
#include "middleware_strings.h"

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
    if (config->req_get_hdr_value_str(req, "Origin", origin_header, sizeof(origin_header)) != ESP_OK) {
        // No Origin header, not a CORS request - continue processing
        return ESP_OK;
    }

    // Check if origin is allowed
    if (!cors_origin_allowed(origin_header, config->allowed_origins)) {
        printf("CORS: Origin '%s' not allowed\n", origin_header);
        config->resp_send_err(req, HTTPD_403_FORBIDDEN, HTTP_ERR_CORS_ORIGIN_NOT_ALLOWED);
        return ESP_FAIL;
    }

    // Handle preflight OPTIONS request
    if (req->method == HTTP_OPTIONS) {
        config->resp_set_status(req, HTTP_STATUS_200_OK);

        // Set CORS headers for preflight
        config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_ORIGIN, origin_header);
        if (config->allowed_methods) {
            config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_METHODS, config->allowed_methods);
        }
        if (config->allowed_headers) {
            config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_HEADERS, config->allowed_headers);
        }
        if (config->allow_credentials) {
            config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_CREDENTIALS, HTTP_CORS_TRUE);
        }
        if (config->max_age > 0) {
            char max_age_str[16];
            sprintf(max_age_str, "%d", config->max_age);
            config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_MAX_AGE, max_age_str);
        }

        config->resp_send(req, NULL, 0);
        printf("CORS: Preflight OPTIONS handled\n");
        return ESP_OK; // Handled OPTIONS request, return without calling handler
    }

    // For non-OPTIONS requests, just add the basic CORS headers
    config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_ORIGIN, origin_header);
    if (config->allow_credentials) {
        config->resp_set_hdr(req, HTTP_HDR_ACCESS_CONTROL_ALLOW_CREDENTIALS, HTTP_CORS_TRUE);
    }

    printf("CORS: Added headers for %s from %s\n", req->uri, origin_header);
    return ESP_OK; // Continue to execute original handler
}
