#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "middleware_content_negotiation.h"
#include <log.h>

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
 * @brief Calculate specificity score per RFC 9110 Section 12.5.1
 *
 * @param media_range Media range string (e.g., "text/html", "text/*", "*\/*")
 * @return Specificity score: 1000 (exact), 100 (type), 10 (universal)
 */
static int calculate_specificity_score(const char *media_range) {
    if (!media_range) {
        return 0;
    }

    // Check for wildcards
    if (strchr(media_range, '*') == NULL) {
        return 1000;  // type/subtype - highest specificity
    } else if (strcmp(media_range, "*/*") == 0) {
        return 10;    // */* - lowest specificity
    } else {
        return 100;   // type/* - medium specificity
    }
}

/**
 * @brief Check if server media type matches client accept range
 *
 * @param accept_range Client accept range (may contain wildcards)
 * @param server_type Server supported media type (no wildcards)
 * @return true if matches, false otherwise
 */
static bool media_type_matches(const char *accept_range, const char *server_type) {
    if (!accept_range || !server_type) {
        return false;
    }

    // Universal wildcard matches everything
    if (strcmp(accept_range, "*/*") == 0) {
        return true;
    }

    // Exact match
    if (strcmp(accept_range, server_type) == 0) {
        return true;
    }

    // Check if accept_range contains wildcards
    const char *wildcard_pos = strstr(accept_range, "/*");
    if (wildcard_pos) {
        // Type wildcard matching (e.g., "text/*" matches "text/html")
        // Find the '/' position in both strings
        const char *accept_slash = strchr(accept_range, '/');
        const char *server_slash = strchr(server_type, '/');

        if (!accept_slash || !server_slash) {
            return false;
        }

        // Compare the type part (before '/')
        size_t type_len = accept_slash - accept_range;
        if (server_slash - server_type != (ptrdiff_t)type_len) {
            return false;
        }

        return strncmp(accept_range, server_type, type_len) == 0;
    }

    return false;
}

/**
 * @brief Safe helper to get negotiation result from request context
 *
 * @param req HTTP request structure
 * @return Negotiation result or NULL if not available
 */
static httpd_content_negotiation_result_t* get_negotiation_result(httpd_req_t *req) {
    if (!req || !req->user_ctx) {
        return NULL;
    }

    // Simply return the user_ctx pointer - the middleware always sets it to NULL
    // when no negotiation result is available, so we can trust it
    return (httpd_content_negotiation_result_t *)req->user_ctx;
}

/**
 * @brief Free negotiation result structure
 *
 * @param result Result structure to free
 */
void httpd_free_negotiation_result(httpd_content_negotiation_result_t *result) {
    if (result) {
        if (result->selected_media_type) {
            free(result->selected_media_type);
            result->selected_media_type = NULL;
        }
        if (result->selected_encoding) {
            free(result->selected_encoding);
            result->selected_encoding = NULL;
        }
        if (result->selected_language) {
            free(result->selected_language);
            result->selected_language = NULL;
        }
        if (result->selected_charset) {
            free(result->selected_charset);
            result->selected_charset = NULL;
        }
        if (result->vary_header_value) {
            free(result->vary_header_value);
            result->vary_header_value = NULL;
        }
        free(result);
    }
}

/**
 * @brief Perform content negotiation per RFC 9110 Part 12
 *
 * @param accept_ranges Parsed Accept header ranges
 * @param capabilities Server capabilities
 * @param result Output negotiation result
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_negotiate_content(const httpd_accept_range_t *accept_ranges,
                                const httpd_content_capabilities_t *capabilities,
                                httpd_content_negotiation_result_t *result) {
    if (!capabilities || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize result structure
    memset(result, 0, sizeof(httpd_content_negotiation_result_t));
    result->media_type_score = -1.0f;
    result->encoding_score = -1.0f;

    // Track if any negotiation occurred
    bool negotiation_used = false;

    // If no accept ranges provided, fall back to first server capability
    if (!accept_ranges) {
        if (capabilities->media_type_count > 0) {
            result->selected_media_type = strdup(capabilities->media_types[0]);
            if (!result->selected_media_type) {
                return ESP_ERR_NO_MEM;
            }
            result->media_type_score = 0.0f;  // Indicate fallback (no negotiation)
        }
        return ESP_OK;
    }

    // If no capabilities available, cannot negotiate
    if (!capabilities->media_types || capabilities->media_type_count == 0) {
        return ESP_OK;  // No capabilities to negotiate with
    }

    // RFC 9110 Section 12.5.1: Score each accept range against server capabilities
    const httpd_accept_range_t *current_range = accept_ranges;
    while (current_range) {
        // Skip unacceptable preferences (RFC 9110 Section 12.4.2)
        if (current_range->quality.value <= 0.0f) {
            current_range = current_range->next;
            continue;
        }

        if (!current_range->range) {
            current_range = current_range->next;
            continue;
        }

        // Check this client preference against server capabilities
        for (size_t i = 0; i < capabilities->media_type_count; i++) {
            const char *server_type = capabilities->media_types[i];
            if (!server_type) {
                continue;
            }

            // Check if server type matches client accept range
            if (!media_type_matches(current_range->range, server_type)) {
                continue;
            }

            // Calculate RFC 9110 score: specificity × quality_value
            int specificity = calculate_specificity_score(current_range->range);
            float score = specificity * current_range->quality.value;

            // Keep track of best match
            negotiation_used = true;
            if (score > result->media_type_score) {
                // Free previous selection
                if (result->selected_media_type) {
                    free(result->selected_media_type);
                }

                result->selected_media_type = strdup(server_type);
                if (!result->selected_media_type) {
                    httpd_free_negotiation_result(result);
                    return ESP_ERR_NO_MEM;
                }

                result->media_type_score = score;

                // Generate Vary header for cache correctness (RFC 9110 Section 12.5.5)
                result->vary_header_needed = true;
                if (result->vary_header_value) {
                    free(result->vary_header_value);
                }
                result->vary_header_value = strdup("Accept");
                if (!result->vary_header_value) {
                    httpd_free_negotiation_result(result);
                    return ESP_ERR_NO_MEM;
                }
            }
        }

        current_range = current_range->next;
    }

    // If no matches found after checking all ranges, fall back to first server capability
    if (!negotiation_used && capabilities->media_type_count > 0) {
        if (result->selected_media_type) {
            free(result->selected_media_type);
        }
        result->selected_media_type = strdup(capabilities->media_types[0]);
        if (!result->selected_media_type) {
            httpd_free_negotiation_result(result);
            return ESP_ERR_NO_MEM;
        }
        result->media_type_score = 0.0f;  // Indicate fallback
    }

    // TODO: Add encoding, language, and charset negotiation in future phases
    // For now, focus on media type negotiation (most important part)

    return ESP_OK;
}

/**
 * @brief Convenience function for simple media type negotiation
 */
esp_err_t httpd_negotiate_media_type(const char *accept_header,
                                   char **available_types,
                                   char **selected_type) {
    if (!available_types || !selected_type) {
        return ESP_ERR_INVALID_ARG;
    }

    *selected_type = NULL;

    // Parse Accept header
    httpd_accept_range_t *ranges = httpd_parse_accept_header(accept_header);
    if (!ranges) {
        // Fall back to first available type
        if (available_types[0]) {
            *selected_type = strdup(available_types[0]);
            return *selected_type ? ESP_OK : ESP_ERR_NO_MEM;
        }
        return ESP_FAIL;
    }

    // Count available types
    size_t count = 0;
    while (available_types[count]) {
        count++;
    }

    // Setup server capabilities
    httpd_content_capabilities_t capabilities = {
        .media_types = available_types,
        .media_type_count = count,
        .encodings = NULL,
        .encoding_count = 0,
        .languages = NULL,
        .language_count = 0,
        .charsets = NULL,
        .charset_count = 0
    };

    // Negotiate
    httpd_content_negotiation_result_t result;
    esp_err_t err = httpd_negotiate_content(ranges, &capabilities, &result);

    // Cleanup parsed ranges
    httpd_free_accept_ranges(ranges);

    if (err != ESP_OK) {
        return err;
    }

    if (result.selected_media_type) {
        *selected_type = result.selected_media_type;
        // Free other result fields but keep selected_media_type
        httpd_free_negotiation_result(&result);
        return ESP_OK;
    }

    httpd_free_negotiation_result(&result);
    return ESP_FAIL;
}

/**
 * @brief Content negotiation middleware
 *
 * Parses request Accept headers and performs content negotiation,
 * storing results in request session context for handlers to use.
 */
esp_err_t middleware_content_negotiation(httpd_req_t *req,
                                       const httpd_uri_t *uri,
                                       void *ctx) {
    httpd_content_negotiation_config_t *config = (httpd_content_negotiation_config_t *)ctx;

    if (!config || !req) {
        LOGD("content_negotiation", "middleware: config=%p, req=%p", config, req);
        return ESP_FAIL;
    }

    // Check if we have a function to get header values (for testing)
    esp_err_t (*get_hdr_value)(httpd_req_t *r, const char *field, char *val, size_t val_size) =
        config->req_get_hdr_value_str ? config->req_get_hdr_value_str :
        (void*)&httpd_req_get_hdr_value_str;

    // Extract Accept header
    char accept_buffer[1024] = {0};
    esp_err_t err = get_hdr_value(req, "Accept", accept_buffer, sizeof(accept_buffer));
    LOGD("content_negotiation", "middleware: Accept header err=%d, buffer='%s'", err, accept_buffer);

    httpd_accept_range_t *accept_ranges = NULL;
    httpd_content_negotiation_result_t *result = NULL;

    // Parse Accept header (if present) - it's OK if not present
    if (err == ESP_OK && accept_buffer[0] != '\0') {
        accept_ranges = httpd_parse_accept_header(accept_buffer);
        LOGD("content_negotiation", "middleware: parsed accept_ranges=%p", accept_ranges);
    }

    // Perform negotiation if we have server capabilities, regardless of Accept header
    // Negotiation should only happen if the request contains an Accept
    // header (currently only Accept is used). Skipping negotiation when
    // no Accept header prevents unnecessary Vary header generation
    // and ensures fallback logic behaves correctly.
    bool should_negotiate = false;
    if (accept_ranges) {
        should_negotiate = config->capabilities.media_type_count > 0 ||
                           config->capabilities.encoding_count > 0 ||
                           config->capabilities.language_count > 0 ||
                           config->capabilities.charset_count > 0;
    }

    LOGD("content_negotiation", "middleware: capabilities - media=%u, encoding=%u, language=%u, charset=%u, should_negotiate=%d",
         config->capabilities.media_type_count, config->capabilities.encoding_count,
         config->capabilities.language_count, config->capabilities.charset_count, should_negotiate);

    if (should_negotiate) {
        result = calloc(1, sizeof(httpd_content_negotiation_result_t));
        LOGD("content_negotiation", "middleware: allocated result=%p", result);
        if (!result) {
            httpd_free_accept_ranges(accept_ranges);
            return ESP_ERR_NO_MEM;
        }

        // Perform content negotiation
        httpd_content_capabilities_t *capabilities;
        if (config->get_capabilities) {
            // Dynamic capabilities
            capabilities = calloc(1, sizeof(httpd_content_capabilities_t));
            if (!capabilities) {
                httpd_free_accept_ranges(accept_ranges);
                free(result);
                return ESP_ERR_NO_MEM;
            }

            err = config->get_capabilities(req, capabilities);
            if (err != ESP_OK) {
                free(capabilities);
                httpd_free_accept_ranges(accept_ranges);
                free(result);
                return err;
            }
        } else {
            // Static capabilities from config
            capabilities = &config->capabilities;
            LOGD("content_negotiation", "middleware: using static capabilities, first media_type='%s'",
                 capabilities->media_types ? capabilities->media_types[0] : "NULL");
        }

        // Negotiate content
        LOGD("content_negotiation", "middleware: calling httpd_negotiate_content with accept_ranges=%p, capabilities=%p",
             accept_ranges, capabilities);
        err = httpd_negotiate_content(accept_ranges, capabilities, result);
        LOGD("content_negotiation", "middleware: negotiate_content returned %d, result->selected_media_type='%s', score=%f",
             err, result->selected_media_type ? result->selected_media_type : "NULL",
             result->media_type_score);

        // Free dynamic capabilities if allocated
        if (config->get_capabilities) {
            free(capabilities);
        }

        if (err != ESP_OK) {
            httpd_free_accept_ranges(accept_ranges);
            if (result) httpd_free_negotiation_result(result);
            free(result);
            return err;
        }

        // Set Vary header if negotiation occurred (cache correctness)
        if (result->vary_header_needed &&
            result->vary_header_value &&
            config->add_vary_header) {
            config->add_vary_header(req, result->vary_header_value);
            LOGD("content_negotiation", "middleware: added Vary header '%s'", result->vary_header_value);
        }
    } else {
        LOGD("content_negotiation", "middleware: should_negotiate=false, result remains NULL");
    }

    // Store results in request user context for handler access
    // Always set the cleanup function when we have allocated memory
    LOGD("content_negotiation", "middleware: setting user_ctx = %p, free_ctx = %p", result, 
         result ? (void*)&httpd_free_negotiation_result : NULL);
    LOGD("content_negotiation", "middleware: req pointer = %p", req);
    
    req->user_ctx = result;
    if (result) {
        // Always set the cleanup function when we have allocated memory
        // This ensures proper cleanup in both test and production environments
        req->free_ctx = (void*)&httpd_free_negotiation_result;
    } else {
        req->free_ctx = NULL;
    }
    
    LOGD("content_negotiation", "middleware: after setting, user_ctx=%p, free_ctx=%p", 
         req->user_ctx, req->free_ctx);

    // Clean up parsed ranges
    httpd_free_accept_ranges(accept_ranges);

    return ESP_OK;
}

/**
 * @brief Get negotiated content type from request context
 *
 * @param req HTTP request structure
 * @return Selected content type or NULL if not negotiated/no context
 */
const char* middleware_get_negotiated_content_type(httpd_req_t *req) {
    httpd_content_negotiation_result_t *result = get_negotiation_result(req);
    return result ? result->selected_media_type : NULL;
}

/**
 * @brief Get negotiated encoding from request context
 *
 * @param req HTTP request structure
 * @return Selected encoding or NULL if not negotiated/no context
 */
const char* middleware_get_negotiated_encoding(httpd_req_t *req) {
    httpd_content_negotiation_result_t *result = get_negotiation_result(req);
    return result ? result->selected_encoding : NULL;
}

/**
 * @brief Get negotiated language from request context
 *
 * @param req HTTP request structure
 * @return Selected language or NULL if not negotiated/no context
 */
const char* middleware_get_negotiated_language(httpd_req_t *req) {
    httpd_content_negotiation_result_t *result = get_negotiation_result(req);
    return result ? result->selected_language : NULL;
}

/**
 * @brief Get negotiated charset from request context
 *
 * @param req HTTP request structure
 * @return Selected charset or NULL if not negotiated/no context
 */
const char* middleware_get_negotiated_charset(httpd_req_t *req) {
    httpd_content_negotiation_result_t *result = get_negotiation_result(req);
    return result ? result->selected_charset : NULL;
}

/**
 * @brief Check if content was negotiated for this request
 *
 * @param req HTTP request structure
 * @return true if negotiation occurred, false otherwise
 */
bool middleware_content_was_negotiated(httpd_req_t *req) {
    httpd_content_negotiation_result_t *result = get_negotiation_result(req);
    if (!result) {
        return false;
    }

    // Check if we have any negotiated content
    return (result->selected_media_type ||
            result->selected_encoding ||
            result->selected_language ||
            result->selected_charset) &&
           (result->media_type_score >= 0.0f ||
            result->encoding_score >= 0.0f);
}

#ifdef __cplusplus
}
#endif
