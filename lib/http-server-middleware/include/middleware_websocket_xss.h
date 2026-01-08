/**
 * @file middleware_websocket_xss.h
 * @brief WebSocket XSS Protection Middleware
 *
 * This middleware provides configurable Cross-Site Scripting (XSS) protection
 * for WebSocket text message payloads. It detects common XSS attack vectors
 * and can either block, sanitize, or log suspicious content.
 *
 * Based on OWASP XSS Prevention Cheat Sheet and RFC 6455 security considerations.
 */

#ifndef _MIDDLEWARE_WEBSOCKET_XSS_H_
#define _MIDDLEWARE_WEBSOCKET_XSS_H_

#include <stdbool.h>
#include <stddef.h>

#include <http_server.h>  // For httpd_req_t, httpd_ws_frame_t
#include <stdint.h>        // For uint8_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Action modes for XSS detection
 */
typedef enum {
    XSS_MODE_BLOCK,          /**< Reject frame with 1003 (Unsupported Data) */
    XSS_MODE_SANITIZE,       /**< Escape dangerous content in-place */
    XSS_MODE_LOG_ONLY,       /**< Allow but log potential threats */
    XSS_MODE_OFF             /**< Disable validation */
} xss_action_mode_t;

/**
 * @brief XSS detection configuration structure
 */
typedef struct xss_detection_config {
    xss_action_mode_t action_mode;       /**< How to handle XSS detection */

    // Detection options - granular control over pattern checking
    bool check_script_tags;              /**< Detect <script> tags */
    bool check_javascript_urls;          /**< Detect javascript: URLs */
    bool check_event_handlers;           /**< Detect on* event handlers */
    bool check_inline_styles;            /**< Detect style= with url(js:) */
    bool check_html_entities;            /**< Detect suspicious HTML entities */

    // Performance limits
    size_t max_payload_scan_size;        /**< Don't scan beyond this size (0 = unlimited) */

    // Callbacks for custom validation
    bool (*custom_xss_checker)(const char *payload, size_t len, void *ctx);
    void *custom_ctx;

    // Logging configuration
    bool enable_logging;                 /**< Enable XSS detection logging */
    uint8_t log_level;                   /**< Log level for XSS events (LOG_* constants) */
} xss_detection_config_t;

/**
 * @brief Initialize default XSS detection configuration
 *
 * @param config Configuration structure to initialize
 */
void xss_detection_config_defaults(xss_detection_config_t *config);

/**
 * @brief WebSocket XSS Protection Middleware
 *
 * This middleware function should be called in WebSocket handlers after
 * receiving a frame payload but before processing it. It provides configurable
 * XSS protection for text message payloads.
 *
 * @param req HTTP request structure (WebSocket context)
 * @param frame WebSocket frame with payload data
 * @param config XSS detection configuration
 * @return ESP_OK on success or no threat detected
 *         ESP_FAIL if XSS detected and action_mode == XSS_MODE_BLOCK
 */
esp_err_t middleware_websocket_xss(httpd_req_t *req,
                                  const httpd_ws_frame_t *frame,
                                  xss_detection_config_t *config);

/**
 * @brief Core XSS pattern detection function
 *
 * Scans payload for XSS attack patterns based on configuration.
 * This is the main detection engine used by the middleware.
 *
 * @param payload Message payload (UTF-8 encoded)
 * @param len Payload length in bytes
 * @param config Detection configuration
 * @return true if XSS patterns detected, false otherwise
 */
bool httpd_contains_xss_pattern(const char *payload, size_t len,
                               const xss_detection_config_t *config);

/**
 * @brief Sanitize XSS-susceptible content
 *
 * Performs in-place escaping of dangerous HTML characters that could be
 * used in XSS attacks. Modifies the payload buffer directly.
 *
 * @param payload Payload buffer to sanitize (modified in-place)
 * @param len Pointer to payload length (updated if buffer expands)
 * @return ESP_OK on success, error code on buffer overflow
 */
esp_err_t httpd_sanitize_xss_payload(char *payload, size_t *len);

/**
 * @brief XSS Detection Pattern Constants
 *
 * Pre-defined constants for common XSS detection patterns.
 * These are used internally but may be useful for custom validators.
 */
#define XSS_PATTERN_SCRIPT_TAG     "<script"
#define XSS_PATTERN_JAVASCRIPT_URL "javascript:"
#define XSS_PATTERN_VBSCRIPT_URL   "vbscript:"
#define XSS_PATTERN_DATA_URL       "data:"
#define XSS_PATTERN_EVENT_HANDLER  " on"
#define XSS_PATTERN_STYLE_URL      "style="

/**
 * @brief XSS Detection Statistics Structure
 *
 * For monitoring and diagnostics, this tracks XSS detection metrics.
 */
typedef struct xss_detection_stats {
    unsigned int total_scans;           /**< Total payloads scanned */
    unsigned int xss_detected;          /**< XSS patterns detected */
    unsigned int frames_blocked;        /**< Frames rejected due to XSS */
    unsigned int frames_sanitized;      /**< Frames with XSS content sanitized */
    unsigned int scan_errors;           /**< Scanning/parsing errors encountered */
} xss_detection_stats_t;

/**
 * @brief Get XSS detection statistics
 *
 * @param stats Statistics structure to populate
 */
void xss_detection_get_stats(xss_detection_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_WEBSOCKET_XSS_H_ */
