/**
 * @file xss_detector.c
 * @brief Core XSS detection logic for WebSocket middleware
 *
 * This file implements pattern-based detection of common XSS attack vectors
 * in WebSocket text message payloads. Designed for embedded systems with
 * limited resources and no regex support.
 */

#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "middleware_websocket_xss.h"
#include <log.h>  // For LOGE, LOGW macros

/**
 * @brief XSS Detection Statistics (global state)
 */
static xss_detection_stats_t g_xss_stats = {0};

/**
 * @brief Case-insensitive string search with length bounds (binary-safe)
 */
static const char* strnistr(const char *haystack, size_t haystack_len, const char *needle) {
    if (!haystack || !needle || haystack_len == 0) return NULL;

    size_t needle_len = strlen(needle);
    if (needle_len == 0) return NULL;
    if (needle_len > haystack_len) return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (strncasecmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

/**
 * @brief Legacy case-insensitive string search (assumes null-termination)
 */
static const char* stristr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;

    size_t needle_len = strlen(needle);
    for (size_t i = 0; haystack[i]; ++i) {
        if (strncasecmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

/**
 * @brief Check for script tag injection
 */
static bool detect_script_tags(const char *payload, size_t len) {
    const char *patterns[] = {
        "<script", "<script ", "< script", "<\tscript",
        "</script>", "</script ", "</ script", "</\tscript"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        if (strnistr(payload, len, patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check for JavaScript URL schemes
 */
static bool detect_javascript_urls(const char *payload, size_t len) {
    const char *patterns[] = {
        "javascript:", "vbscript:", "data:"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        if (strnistr(payload, len, patterns[i]) != NULL) {
            return true;
        }
    }

    // Also check for URL-encoded variants
    if (strnistr(payload, len, "%6A%61%76%61%73%63%72%69%70%74%3A") != NULL) { // javascript:
        return true;
    }

    return false;
}

/**
 * @brief Check for event handler injection
 */
static bool detect_event_handlers(const char *payload, size_t len) {
    const char *patterns[] = {
        " on", " onblur=", " onchange=", " onclick=",
        " ondblclick=", " onfocus=", " onkeydown=", " onkeypress=",
        " onkeyup=", " onload=", " onmousedown=", " onmousemove=",
        " onmouseout=", " onmouseover=", " onmouseup=", " onreset=",
        " onselect=", " onsubmit=", " onunload="
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        if (strnistr(payload, len, patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check for inline styles with dangerous URLs
 */
static bool detect_inline_styles(const char *payload, size_t len) {
    const char *style_pos = strnistr(payload, len, "style=");
    if (!style_pos) return false;

    // Look for javascript: or similar within the style attribute
    const char *end_quote = NULL;
    if (style_pos[6] == '"' || style_pos[6] == '\'') {
        end_quote = strchr(style_pos + 7, style_pos[6]);
    }

    const char *search_end = end_quote ? end_quote : payload + len;
    const char *substr = style_pos;

    while ((substr = strnistr(substr, len - (substr - payload), "url(")) != NULL && substr < search_end) {
        // Check if url( contains javascript:
        const char *js_url = strnistr(substr, len - (substr - payload), "javascript:");
        if (js_url && js_url < search_end) {
            return true;
        }
        substr += 4; // Move past "url("
    }

    return false;
}

/**
 * @brief Check for suspicious HTML entities
 */
static bool detect_html_entities(const char *payload, size_t len) {
    // Look for encoded script tags and other suspicious entities
    const char *patterns[] = {
        "&#x3C;script", "&#60;script", "&#x3C;/script", "&#60;/script",
        "<script", ">/script"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        if (strnistr(payload, len, patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if payload should be scanned based on size limits
 */
static bool should_scan_payload(const xss_detection_config_t *config, size_t len) {
    if (config->max_payload_scan_size == 0) {
        return true; // No limit
    }
    return len <= config->max_payload_scan_size;
}

void xss_detection_config_defaults(xss_detection_config_t *config) {
    if (!config) return;

    memset(config, 0, sizeof(*config));
    config->action_mode = XSS_MODE_BLOCK;
    config->check_script_tags = true;
    config->check_javascript_urls = true;
    config->check_event_handlers = true;
    config->check_inline_styles = true;
    config->check_html_entities = true;
    config->max_payload_scan_size = 4096; // 4KB limit
    config->enable_logging = true;
    config->log_level = LOG_WARN;
}

bool httpd_contains_xss_pattern(const char *payload, size_t len,
                               const xss_detection_config_t *config) {
    if (!payload || !config || len == 0) {
        return false;
    }

    g_xss_stats.total_scans++;

    // Check size limits
    if (!should_scan_payload(config, len)) {
        if (config->enable_logging && config->log_level >= LOG_DEBUG) {
            LOGD("XSS_DETECTOR", "Payload too large (%zu bytes), skipping scan", len);
        }
        return false;
    }

    bool detected = false;

    // Run enabled detection checks
    if (config->check_script_tags && detect_script_tags(payload, len)) {
        detected = true;
        if (config->enable_logging && config->log_level >= LOG_WARN) {
            LOGW("XSS_DETECTOR", "Script tag detected in payload");
        }
    }

    if (!detected && config->check_javascript_urls && detect_javascript_urls(payload, len)) {
        detected = true;
        if (config->enable_logging && config->log_level >= LOG_WARN) {
            LOGW("XSS_DETECTOR", "JavaScript URL detected in payload");
        }
    }

    if (!detected && config->check_event_handlers && detect_event_handlers(payload, len)) {
        detected = true;
        if (config->enable_logging && config->log_level >= LOG_WARN) {
            LOGW("XSS_DETECTOR", "Event handler detected in payload");
        }
    }

    if (!detected && config->check_inline_styles && detect_inline_styles(payload, len)) {
        detected = true;
        if (config->enable_logging && config->log_level >= LOG_WARN) {
            LOGW("XSS_DETECTOR", "Inline style with dangerous URL detected");
        }
    }

    if (!detected && config->check_html_entities && detect_html_entities(payload, len)) {
        detected = true;
        if (config->enable_logging && config->log_level >= LOG_WARN) {
            LOGW("XSS_DETECTOR", "Suspicious HTML entity detected");
        }
    }

    // Custom checker callback
    if (!detected && config->custom_xss_checker) {
        if (config->custom_xss_checker(payload, len, config->custom_ctx)) {
            detected = true;
            if (config->enable_logging && config->log_level >= LOG_WARN) {
                LOGW("XSS_DETECTOR", "Custom XSS checker detected threat");
            }
        }
    }

    if (detected) {
        g_xss_stats.xss_detected++;
    }

    return detected;
}

esp_err_t httpd_sanitize_xss_payload(char *payload, size_t *len) {
    if (!payload || !len || *len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Simple HTML entity encoding for dangerous characters
    // This is a basic implementation - in production you might want more sophisticated sanitization

    const char dangerous[] = "<>&\"'";
    const char *replacements[] = {"<", ">", "&", "\"", "&#x27;"};

    size_t input_len = *len;
    size_t output_len = input_len * 6 + 1; // Worst case: each char becomes </> (6 chars)

    // For embedded systems, we'll do in-place replacement which may truncate
    // In a more sophisticated implementation, you'd allocate a larger buffer

    char *output = calloc(output_len, sizeof(char));
    if (!output) {
        return ESP_ERR_NO_MEM;
    }

    size_t out_pos = 0;
    for (size_t i = 0; i < input_len && out_pos < output_len - 6; ++i) {
        bool replaced = false;
        for (size_t j = 0; dangerous[j]; ++j) {
            if (payload[i] == dangerous[j]) {
                strcpy(output + out_pos, replacements[j]);
                out_pos += strlen(replacements[j]);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            output[out_pos++] = payload[i];
        }
    }
    output[out_pos] = '\0';

    // Copy back to original buffer (may truncate if too long)
    size_t new_len = strlen(output);
    if (new_len >= *len) {
        new_len = *len - 1; // Reserve space for null terminator
        memcpy(payload, output, new_len);
        payload[new_len] = '\0';
    } else {
        strcpy(payload, output);
    }

    free(output);
    *len = strlen(payload);

    g_xss_stats.frames_sanitized++;

    return ESP_OK;
}

void xss_detection_get_stats(xss_detection_stats_t *stats) {
    if (stats) {
        memcpy(stats, &g_xss_stats, sizeof(g_xss_stats));
    }
}
