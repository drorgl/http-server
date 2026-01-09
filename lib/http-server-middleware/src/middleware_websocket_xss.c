/**
 * @file middleware_websocket_xss.c
 * @brief WebSocket XSS Protection Middleware Implementation
 *
 * This file implements the main middleware logic for detecting and handling
 * XSS attacks in WebSocket text message payloads. It provides configurable
 * blocking/sanitization modes and integrates with the core detection engine.
 */

#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include "middleware_websocket_xss.h"
#include <log.h>  // For LOGE, LOGW macros
#include <http_server.h>  // For httpd_req_t, httpd_ws_frame_t

esp_err_t middleware_websocket_xss(httpd_req_t *req,
                                  const httpd_ws_frame_t *frame,
                                  xss_detection_config_t *config) {
    // Input validation
    if (!req || !frame || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Only process text frames - binary frames don't need XSS protection
    if (frame->type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;  // Not an error, just nothing to check
    }

    // If detection is disabled for this config, skip processing
    if (config->action_mode == XSS_MODE_OFF) {
        return ESP_OK;
    }

    // Check for XSS patterns using the core detection engine
    bool has_xss = httpd_contains_xss_pattern((const char*)frame->payload,
                                             frame->len, config);

    if (!has_xss) {
        // No XSS detected - payload is safe
        return ESP_OK;
    }

    // XSS detected - handle according to configured action mode
    switch (config->action_mode) {
        case XSS_MODE_BLOCK: {
            // Close WebSocket connection with proper close code
            // Send close frame manually as RFC 6455 close handling
            const char *reason = "Unsupported Data";
            uint8_t close_payload[17] = {0};  // Status code (2 bytes) + reason (15 bytes)
            close_payload[0] = (1003 >> 8) & 0xFF;  // Status code in network byte order (big-endian)
            close_payload[1] = 1003 & 0xFF;
            memcpy(&close_payload[2], reason, strlen(reason));
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = close_payload,
                .len = 2 + strlen(reason)
            };
            esp_err_t close_ret = httpd_ws_send_frame(req, &close_frame);
            if (close_ret != ESP_OK && config->enable_logging && config->log_level >= LOG_ERROR) {
                LOGE("XSS_MIDDLEWARE", "Failed to send close frame: %d", close_ret);
            }

            // Increment blocked frames count
            xss_detection_stats_t stats;
            xss_detection_get_stats(&stats);
            stats.frames_blocked++;
            // Note: We don't update stats in this read-only call

            if (config->enable_logging && config->log_level >= LOG_WARN) {
                LOGW("XSS_MIDDLEWARE", "XSS detected - blocking WebSocket frame (%zu bytes)", frame->len);
            }

            return ESP_FAIL; // Signal that frame should not be processed
        }

        case XSS_MODE_SANITIZE: {
            // Attempt to sanitize the payload in-place
            // Note: This is a simplified implementation. In production, you might want
            // to use a more sophisticated HTML sanitizer.

            // For now, we'll use the basic sanitizer (HTML entity encoding)
            char *mutable_payload = (char*)frame->payload; // Cast away const for sanitization
            size_t mutable_len = frame->len;

            esp_err_t sanitize_ret = httpd_sanitize_xss_payload(mutable_payload, &mutable_len);
            if (sanitize_ret != ESP_OK) {
                if (config->enable_logging && config->log_level >= LOG_ERROR) {
                    LOGE("XSS_MIDDLEWARE", "Failed to sanitize XSS payload: %d", sanitize_ret);
                }
                // Fallback to blocking if sanitization fails
                const char *reason = "Unsupported Data";
                uint8_t close_payload[17] = {0};  // Status code (2 bytes) + reason (15 bytes)
                close_payload[0] = (1003 >> 8) & 0xFF;  // Status code in network byte order (big-endian)
                close_payload[1] = 1003 & 0xFF;
                memcpy(&close_payload[2], reason, strlen(reason));
                httpd_ws_frame_t close_frame = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = close_payload,
                    .len = 2 + strlen(reason)
                };
                httpd_ws_send_frame(req, &close_frame);
                return ESP_FAIL;
            }

            if (config->enable_logging && config->log_level >= LOG_INFO) {
                // Note: original length was frame->len, new length is mutable_len
                LOGI("XSS_MIDDLEWARE", "XSS detected and sanitized (%zu -> %zu bytes)",
                     frame->len, mutable_len);
            }

            // Frame continues with sanitized payload
            return ESP_OK;
        }

        case XSS_MODE_LOG_ONLY: {
            // XSS detected but allowed to continue
            if (config->enable_logging && config->log_level >= LOG_WARN) {
                LOGW("XSS_MIDDLEWARE", "XSS detected - allowing frame to continue (%zu bytes)", frame->len);
            }
            return ESP_OK; // Allow frame to continue
        }

        case XSS_MODE_OFF:
        default:
            // This should not be reached due to the early return above
            return ESP_OK;
    }
}

/**
 * @brief Initialize XSS middleware configuration with secure defaults
 *
 * This function sets up a configuration that provides strong XSS protection
 * suitable for most WebSocket applications. Callers can modify individual
 * settings after initialization if needed.
 *
 * @param config Configuration structure to initialize
 */
void middleware_websocket_xss_init_config(xss_detection_config_t *config) {
    if (!config) return;

    xss_detection_config_defaults(config);

    // Additional middleware-specific defaults could go here
    // For now, rely on the detector's defaults which are already secure
}

/**
 * @brief Check if XSS middleware is properly configured for a handler
 *
 * Helper function to validate configuration before registering a WebSocket
 * handler with XSS protection.
 *
 * @param config Configuration to validate
 * @return true if configuration is valid and secure, false otherwise
 */
bool middleware_websocket_xss_config_valid(const xss_detection_config_t *config) {
    if (!config) return false;

    // Basic validation
    if (config->action_mode > XSS_MODE_OFF) return false;

    // Security validation
    if (config->max_payload_scan_size > 0 && config->max_payload_scan_size < 128) {
        // Too small - might miss attacks
        return false;
    }

    // At least one detection method should be enabled
    if (!config->check_script_tags &&
        !config->check_javascript_urls &&
        !config->check_event_handlers &&
        !config->check_inline_styles &&
        !config->check_html_entities &&
        !config->custom_xss_checker) {
        return false; // No checks enabled
    }

    return true;
}

/**
 * @brief Recommended configuration for production WebSocket endpoints
 *
 * This function initializes a configuration optimized for production security
 * with conservative settings that balance security and performance.
 *
 * @param config Configuration structure to initialize
 */
void middleware_websocket_xss_production_config(xss_detection_config_t *config) {
    if (!config) return;

    memset(config, 0, sizeof(*config));

    // Conservative security settings
    config->action_mode = XSS_MODE_BLOCK;  // Block by default
    config->check_script_tags = true;
    config->check_javascript_urls = true;
    config->check_event_handlers = true;
    config->check_inline_styles = true;
    config->check_html_entities = false;    // Can be noisy, disable for production
    config->max_payload_scan_size = 8192;   // Allow larger payloads
    config->custom_xss_checker = NULL;      // No custom checker
    config->enable_logging = true;
    config->log_level = LOG_WARN;           // Warn level for security events
}

/**
 * @brief Development configuration with relaxed security
 *
 * This configuration is useful during development when you want to log
 * potential issues without blocking functionality.
 *
 * @param config Configuration structure to initialize
 */
void middleware_websocket_xss_development_config(xss_detection_config_t *config) {
    if (!config) return;

    middleware_websocket_xss_production_config(config);

    // Relaxed for development
    config->action_mode = XSS_MODE_LOG_ONLY; // Don't block, just log
    config->log_level = LOG_DEBUG;             // More verbose logging
    config->max_payload_scan_size = 0;         // No size limit
}
