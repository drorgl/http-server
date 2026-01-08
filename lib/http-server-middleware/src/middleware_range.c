#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../include/middleware_range.h"
#include "../include/middleware_strings.h"  // For safe string operations

/**
 * @brief Range parsing states for finite state machine
 */
typedef enum {
    PARSE_STATE_UNIT,      /* Parse range unit (e.g., "bytes") */
    PARSE_STATE_RANGES,    /* Parse range specifications */
    PARSE_STATE_RANGE,     /* Parse individual range */
    PARSE_STATE_COMPLETE,  /* Parsing complete */
    PARSE_STATE_ERROR      /* Parse error */
} range_parse_state_t;

/**
 * @brief Check if a request has a Range header
 */
bool httpd_req_has_range_header(httpd_req_t *req) {
    if (!req) {
        return false;
    }

    // This function is not mockable with the current design
    // For testing, we'll need to test the middleware directly
    size_t range_header_len = httpd_req_get_hdr_value_len(req, "Range");
    return range_header_len > 0;
}

/**
 * @brief Validate range specification against total length
 */
static bool validate_range_spec(const httpd_range_spec_t *spec, long long total_length) {
    // RFC 9110 Section 14.1.2 - Range specification validation

    // Start position must be valid
    if (spec->has_start && spec->start < 0) {
        return false;
    }

    // End position must be valid
    if (spec->has_end && spec->end < 0) {
        return false;
    }

    // Range must be satisfiable
    if (spec->has_start && spec->has_end && spec->start > spec->end) {
        return false;
    }

    // Range must not exceed content length (if known)
    if (total_length >= 0) {
        long long effective_end = spec->has_end ? spec->end : (total_length - 1);
        if (spec->has_start && spec->start >= total_length) {
            return false;
        }
        if (effective_end >= total_length) {
            effective_end = total_length - 1;
        }
    }

    return true;
}

/**
 * @brief Parse a single byte range specification
 */
static bool parse_byte_range_spec(const char *range_str, httpd_range_spec_t *spec) {
    const char *ptr = range_str;

    // Skip whitespace
    while (*ptr && isspace((unsigned int)*ptr)) ptr++;

    // Check for single number (suffix-byte-range)
    if (*ptr == '-') {
        // Suffix range: -<suffix-length>
        ptr++;
        // Skip whitespace
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;

        if (!*ptr || !isdigit((unsigned char)*ptr)) {
            return false;
        }

        char *endptr;
        long long suffix_len = strtoll(ptr, &endptr, 10);

        // Check for conversion errors and valid range
        if (*endptr != '\0' && !isspace((unsigned int)*endptr)) {
            return false;
        }

        if (suffix_len <= 0) {
            return false;
        }

        spec->has_start = false;
        spec->has_end = true;
        spec->start = 0; // Will be calculated when total length is known
        spec->end = suffix_len;
        return true;
    }

    // Parse first number (start position)
    if (!isdigit((unsigned char)*ptr)) {
        return false;
    }

    char *endptr;
    long long start_pos = strtoll(ptr, &endptr, 10);
    ptr = endptr;

    // Skip whitespace
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    if (*ptr != '-') {
        return false;
    }
    ptr++;

    // Skip whitespace
    while (*ptr && isspace((unsigned int)*ptr)) ptr++;

    if (*ptr == '\0' || isspace((unsigned int)*ptr)) {
        // Open-ended range: <start>-
        spec->has_start = true;
        spec->has_end = false;
        spec->start = start_pos;
        spec->end = -1;
        return true;
    }

    // Parse second number (end position)
    if (!isdigit((unsigned char)*ptr)) {
        return false;
    }

    long long end_pos = strtoll(ptr, &endptr, 10);
    ptr = endptr;

    // Skip whitespace
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    // Should be at end of string
    if (*ptr != '\0') {
        return false;
    }

    spec->has_start = true;
    spec->has_end = true;
    spec->start = start_pos;
    spec->end = end_pos;
    return true;
}

/**
 * @brief Parse Range header value into range request structure
 */
static esp_err_t parse_range_header_value(const char *range_value, long long content_length,
                                        httpd_range_request_t *range_req) {
    if (!range_value || !range_req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize structure
    range_req->is_valid = false;
    range_req->range_unit = NULL;
    range_req->range_count = 0;
    range_req->ranges = NULL;
    range_req->total_length = content_length;

    const char *ptr = range_value;

    // Parse range unit (typically "bytes")
    while (*ptr && isspace((unsigned int)*ptr)) ptr++;

    const char *unit_start = ptr;
    while (*ptr && !isspace((unsigned int)*ptr) && *ptr != '=') ptr++;

    if (*ptr != '=') {
        return ESP_ERR_INVALID_ARG;
    }

    size_t unit_len = ptr - unit_start;
    range_req->range_unit = (char*)malloc(unit_len + 1);
    if (!range_req->range_unit) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(range_req->range_unit, unit_start, unit_len);
    range_req->range_unit[unit_len] = '\0';

    // Only support "bytes" range unit for now
    if (strcmp(range_req->range_unit, "bytes") != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ptr++; // Skip '='

    // Parse range specifications
    const char *ranges_start = ptr;

    // Count maximum number of ranges for allocation
    size_t max_ranges = MAX_RANGE_SPECS;
    range_req->ranges = (httpd_range_spec_t*)malloc(max_ranges * sizeof(httpd_range_spec_t));
    if (!range_req->ranges) {
        free(range_req->range_unit);
        range_req->range_unit = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Parse comma-separated range specifications
    bool parsing_ranges = true;
    while (parsing_ranges && range_req->range_count < max_ranges) {
        // Find next comma or end
        const char *range_end = ptr;
        while (*range_end && *range_end != ',') range_end++;

        // Extract single range string
        size_t range_len = range_end - ptr;
        if (range_len == 0) {
            break;
        }

        char *range_str = (char*)malloc(range_len + 1);
        if (!range_str) {
            break;
        }

        memcpy(range_str, ptr, range_len);
        range_str[range_len] = '\0';

        // Trim whitespace
        char *trim_start = range_str;
        while (*trim_start && isspace((unsigned int)*trim_start)) trim_start++;

        char *trim_end = range_str + range_len - 1;
        while (trim_end > trim_start && isspace((unsigned int)*trim_end)) trim_end--;
        trim_end[1] = '\0';

        // Parse the range specification
        if (parse_byte_range_spec(trim_start, &range_req->ranges[range_req->range_count])) {
            range_req->range_count++;
        } else {
            // Invalid range specification, skip it but continue parsing
        }

        free(range_str);

        if (*range_end == ',') {
            ptr = range_end + 1;
        } else {
            parsing_ranges = false;
        }
    }

    // Validate all range specifications
    bool all_valid = true;
    for (size_t i = 0; i < range_req->range_count; i++) {
        if (!validate_range_spec(&range_req->ranges[i], content_length)) {
            all_valid = false;
            break;
        }
    }

    if (!all_valid || range_req->range_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // For single range and suffix ranges, calculate actual byte positions
    if (range_req->range_count == 1) {
        httpd_range_spec_t *spec = &range_req->ranges[0];

        // Convert suffix ranges to actual byte ranges
        if (!spec->has_start && spec->has_end && content_length >= 0) {
            long long suffix_len = spec->end;
            if (suffix_len > content_length) {
                suffix_len = content_length;
            }
            spec->start = content_length - suffix_len;
            spec->end = content_length - 1;
            spec->has_start = true;
        }

        // Ensure end position doesn't exceed content length
        if (content_length >= 0 && spec->has_end && spec->end >= content_length) {
            spec->end = content_length - 1;
        }
    }

    range_req->is_valid = true;
    return ESP_OK;
}

/**
 * @brief Parse Range header from request
 */
httpd_range_request_t* httpd_parse_range_header(httpd_req_t *req, const httpd_range_middleware_config_t *config) {
    if (!req || !config) {
        return NULL;
    }

    // Get Range header value
    char range_value[256]; // Reasonable limit for range header
    memset(range_value, 0, sizeof(range_value));
    esp_err_t ret;
    if (config->req_get_hdr_value_str) {
        ret = config->req_get_hdr_value_str(req, "Range", range_value, sizeof(range_value));
    } else {
        ret = httpd_req_get_hdr_value_str(req, "Range", range_value, sizeof(range_value));
    }
    if (ret != ESP_OK) {
        return NULL;
    }

    // Allocate range request structure
    httpd_range_request_t *range_req = (httpd_range_request_t*)malloc(sizeof(httpd_range_request_t));
    if (!range_req) {
        return NULL;
    }

    // Parse the range header
    ret = parse_range_header_value(range_value, config->content_length, range_req);
    if (ret != ESP_OK) {
        httpd_range_free(range_req);
        return NULL;
    }

    return range_req;
}

/**
 * @brief Free range request structure
 */
void httpd_range_free(httpd_range_request_t *range_req) {
    if (!range_req) {
        return;
    }

    if (range_req->range_unit) {
        free(range_req->range_unit);
    }

    if (range_req->ranges) {
        free(range_req->ranges);
    }

    free(range_req);
}

/**
 * @brief Generate Content-Range header value
 */
static esp_err_t generate_content_range_header(char *buffer, size_t buffer_size,
                                             long long start, long long end,
                                             long long total_length) {
    // RFC 9110 Section 14.4 - Content-Range header format
    // Content-Range: <unit> <range-start>-<range-end>/<size>
    // Content-Range: <unit> <range-start>-<range-end>/*

    int written;
    if (total_length >= 0) {
        written = snprintf(buffer, buffer_size, "bytes %lld-%lld/%lld", start, end, total_length);
    } else {
        written = snprintf(buffer, buffer_size, "bytes %lld-%lld/*", start, end);
    }

    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/**
 * @brief Send partial content response with Content-Range header
 */
esp_err_t httpd_resp_send_partial_content(httpd_req_t *req,
                                        const char *data,
                                        size_t data_len,
                                        long long range_start,
                                        long long range_end,
                                        long long total_length) {
    if (!req || !data || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set 206 Partial Content status
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_206);
    if (ret != ESP_OK) {
        return ret;
    }

    // Generate and send Content-Range header
    char content_range_hdr[64];
    ret = generate_content_range_header(content_range_hdr, sizeof(content_range_hdr),
                                       range_start, range_end, total_length);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = httpd_resp_set_hdr(req, "Content-Range", content_range_hdr);
    if (ret != ESP_OK) {
        return ret;
    }

    // Set Content-Length header
    char content_len_str[32];
    int written = snprintf(content_len_str, sizeof(content_len_str), "%zu", data_len);
    if (written < 0 || (size_t)written >= sizeof(content_len_str)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ret = httpd_resp_set_hdr(req, "Content-Length", content_len_str);
    if (ret != ESP_OK) {
        return ret;
    }

    // Accept-Ranges header (RFC 9110 recommendation)
    ret = httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");
    if (ret != ESP_OK) {
        return ret;
    }

    // Send the partial content
    return httpd_resp_send(req, data, data_len);
}

/**
 * @brief Send 416 Range Not Satisfiable response
 */
esp_err_t httpd_resp_send_range_not_satisfiable(httpd_req_t *req, long long total_length) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set 416 Range Not Satisfiable status
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_416);
    if (ret != ESP_OK) {
        return ret;
    }

    // Generate Content-Range header indicating total size or unknown (RFC 9110 Section 14.4)
    char content_range_hdr[32];
    if (total_length >= 0) {
        int written = snprintf(content_range_hdr, sizeof(content_range_hdr), "bytes */%lld", total_length);
        if (written < 0 || (size_t)written >= sizeof(content_range_hdr)) {
            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        int written = snprintf(content_range_hdr, sizeof(content_range_hdr), "bytes */*");
        if (written < 0 || (size_t)written >= sizeof(content_range_hdr)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    ret = httpd_resp_set_hdr(req, "Content-Range", content_range_hdr);
    if (ret != ESP_OK) {
        return ret;
    }

    // Send empty body
    return httpd_resp_send(req, NULL, 0);
}

/**
 * @brief Range requests middleware function
 */
esp_err_t middleware_range(httpd_req_t *req, const httpd_uri_t *uri, void *ctx) {
    if (!req || !ctx) {
        return ESP_OK; // Continue processing
    }

    httpd_range_middleware_config_t *config = (httpd_range_middleware_config_t*)ctx;

    // Check if If-Range precondition failed (set by conditional middleware)
    // This is a temporary implementation - should be improved with proper inter-middleware communication
    if (req->user_ctx == (void*)0x1) {
        // Reset user_ctx to avoid affecting other code
        req->user_ctx = NULL;
        return ESP_OK; // Continue to normal handler, ignore Range header
    }

    // Check for Range header - use callback if provided, otherwise fallback to direct call
    bool has_range_header = config->req_has_range_header ?
        config->req_has_range_header(req) : httpd_req_has_range_header(req);

    if (!has_range_header) {
        return ESP_OK; // No range header, continue to normal handler
    }

    // Parse and validate range header - use callback if provided, otherwise fallback to direct call
    httpd_range_request_t *range_req = config->req_parse_range_header ?
        config->req_parse_range_header(req, config->content_length) :
        httpd_parse_range_header(req, config);

    if (!range_req) {
        // Malformed range header, send 416 - use callback if provided, otherwise fallback to direct call
        if (config->resp_send_range_not_satisfiable) {
            config->resp_send_range_not_satisfiable(req, config->content_length);
        } else {
            httpd_resp_send_range_not_satisfiable(req, config->content_length);
        }
        return ESP_FAIL; // Stop processing
    }

    // Check if range request is valid
    if (!range_req->is_valid) {
        httpd_range_free(range_req);
        if (config->resp_send_range_not_satisfiable) {
            config->resp_send_range_not_satisfiable(req, config->content_length);
        } else {
            httpd_resp_send_range_not_satisfiable(req, config->content_length);
        }
        return ESP_FAIL;
    }

    // For now, only support single ranges (as per design document initial implementation)
    if (range_req->range_count > 1 && !config->enable_multiple_ranges) {
        httpd_range_free(range_req);
        if (config->resp_send_range_not_satisfiable) {
            config->resp_send_range_not_satisfiable(req, config->content_length);
        } else {
            httpd_resp_send_range_not_satisfiable(req, config->content_length);
        }
        return ESP_FAIL;
    }

    // Call user-defined range handler
    if (config->handler) {
        httpd_range_response_ctx_t response_ctx = {
            .request = range_req,
            .content_length = config->content_length,
            .content_type = config->content_type,
            .user_ctx = config->context
        };

        esp_err_t ret = config->handler(req, &response_ctx);
        httpd_range_free(range_req);
        return ret;
    }

    // No handler configured, send 416
    httpd_range_free(range_req);
    if (config->resp_send_range_not_satisfiable) {
        config->resp_send_range_not_satisfiable(req, config->content_length);
    } else {
        httpd_resp_send_range_not_satisfiable(req, config->content_length);
    }
    return ESP_FAIL;
}
