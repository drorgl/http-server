#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "middleware_content_negotiation.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse quality value from q parameter
 *
 * @param q_str Quality string (e.g., "q=0.8")
 * @param quality Output quality value
 * @return true on success, false on error
 */
static bool parse_quality_value(const char *q_str, httpd_quality_value_t *quality) {
    if (!q_str || !quality) {
        return false;
    }

    // Skip "q=" prefix
    if (q_str[0] != 'q' || q_str[1] != '=') {
        return false;
    }

    const char *value_str = q_str + 2;
    float value = strtof(value_str, NULL);

    if (value < 0.0f || value > 1.0f) {
        return false;
    }

    quality->value = value;
    quality->explicit = true;
    return true;
}

/**
 * @brief Parse a single accept range
 *
 * @param range_str Range string (e.g., "text/html;q=0.8")
 * @param range Output range structure
 * @return true on success, false on error
 */
static bool parse_accept_range(const char *range_str, httpd_accept_range_t *range) {
    if (!range_str || !range) {
        return false;
    }

    // Find quality parameter if present
    const char *q_pos = strstr(range_str, ";q=");
    size_t range_len;

    if (q_pos) {
        range_len = q_pos - range_str;
        if (!parse_quality_value(q_pos + 1, &range->quality)) {
            return false;
        }
    } else {
        range_len = strlen(range_str);
        range->quality.value = 1.0;
        range->quality.explicit = false;
    }

    // Trim whitespace
    const char *start = range_str;
    const char *end = range_str + range_len - 1;

    while (start <= end && isspace(*start)) start++;
    while (end >= start && isspace(*end)) end--;

    if (start > end) {
        return false;
    }

    size_t trimmed_len = end - start + 1;

    // For now, store range as string (foundation - no parameter handling yet)
    range->range = malloc(trimmed_len + 1);
    if (range->range) {
        memcpy(range->range, start, trimmed_len);
        range->range[trimmed_len] = '\0';
    }
    range->parameters = NULL;
    range->next = NULL;

    return range->range != NULL;
}

/**
 * @brief Parse Accept header with quality values
 *
 * @param header_value Raw Accept header value
 * @return Parsed accept ranges or NULL on error
 */
httpd_accept_range_t* httpd_parse_accept_header(const char *header_value) {
    if (!header_value || !*header_value) {
        return NULL;
    }

    httpd_accept_range_t *head = NULL;
    httpd_accept_range_t *tail = NULL;

    // Make a copy to tokenize
    char *header_copy = strdup(header_value);
    if (!header_copy) {
        return NULL;
    }

    char *saveptr;
    char *token = strtok_r(header_copy, ",", &saveptr);

    while (token) {
        // Trim leading/trailing whitespace
        while (*token && isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end >= token && isspace(*end)) {
            *end = '\0';
            end--;
        }

        if (*token) {
            httpd_accept_range_t *range = calloc(1, sizeof(httpd_accept_range_t));
            if (!range) {
                break;
            }

            if (!parse_accept_range(token, range)) {
                free(range);
                break;
            }

            // Add to linked list
            if (!head) {
                head = tail = range;
            } else {
                tail->next = range;
                tail = range;
            }
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    free(header_copy);

    // If parsing failed, cleanup
    if (token) {
        httpd_free_accept_ranges(head);
        return NULL;
    }

    return head;
}

/**
 * @brief Free accept ranges structure
 */
void httpd_free_accept_ranges(httpd_accept_range_t *ranges) {
    while (ranges) {
        httpd_accept_range_t *next = ranges->next;
        if (ranges->range) {
            free(ranges->range);
        }
        if (ranges->parameters) {
            free(ranges->parameters);
        }
        free(ranges);
        ranges = next;
    }
}

/**
 * @brief Perform content negotiation
 *
 * @param accept_ranges Parsed Accept header ranges
 * @param capabilities Server capabilities
 * @param result Output negotiation result
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_negotiate_content(const httpd_accept_range_t *accept_ranges,
                                const httpd_content_capabilities_t *capabilities,
                                httpd_content_negotiation_result_t *result) {
    // TODO: Implement RFC 9110 content negotiation algorithm
    (void)accept_ranges;
    (void)capabilities;
    (void)result;
    return ESP_FAIL;
}

/**
 * @brief Convenience function for simple media type negotiation
 */
esp_err_t httpd_negotiate_media_type(const char *accept_header,
                                   char **available_types,
                                   char **selected_type) {
    // TODO: Implement convenience negotiation function
    (void)accept_header;
    (void)available_types;
    (void)selected_type;
    return ESP_FAIL;
}

/**
 * @brief Content negotiation middleware
 *
 * Parses request Accept headers and performs content negotiation,
 * storing results in request user context for handlers to use.
 */
esp_err_t middleware_content_negotiation(httpd_req_t *req,
                                       const httpd_uri_t *uri,
                                       void *ctx) {
    httpd_content_negotiation_config_t *config = (httpd_content_negotiation_config_t *)ctx;

    if (!config) {
        return ESP_FAIL;
    }

    // TODO: Implement middleware logic
    // 1. Extract Accept* headers
    // 2. Parse headers
    // 3. Perform negotiation
    // 4. Store results in request context
    // 5. Set Vary headers if needed

    (void)req;
    (void)uri;

    // For now, just pass through
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
