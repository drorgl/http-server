#ifndef _MIDDLEWARE_CONTENT_NEGOTIATION_H_
#define _MIDDLEWARE_CONTENT_NEGOTIATION_H_

#include <stdbool.h>
#include <stddef.h>

#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Quality value representation
 */
typedef struct httpd_quality_value {
    float value;                     /**< Quality value (0.000 to 1.000) */
    bool explicit;                   /**< true if explicitly set, false for default 1.0 */
} httpd_quality_value_t;

/**
 * @brief Accept header range with quality value
 */
typedef struct httpd_accept_range {
    char *range;                     /**< Media range (e.g., "text/plain", "*\/*") */
    httpd_quality_value_t quality;   /**< Associated quality value */
    char *parameters;                /**< Media type parameters if any */
    struct httpd_accept_range *next; /**< Linked list for multiple ranges */
} httpd_accept_range_t;

/**
 * @brief Server content capabilities
 */
typedef struct httpd_content_capabilities {
    // Media type capabilities
    char **media_types;              /**< NULL-terminated array of supported media types */
    size_t media_type_count;         /**< Number of media types */

    // Encoding capabilities
    char **encodings;                /**< NULL-terminated array of supported encodings */
    size_t encoding_count;           /**< Number of encodings */

    // Language capabilities
    char **languages;                /**< NULL-terminated array of supported languages */
    size_t language_count;           /**< Number of languages */

    // Charset capabilities
    char **charsets;                 /**< NULL-terminated array of supported charsets */
    size_t charset_count;            /**< Number of charsets */
} httpd_content_capabilities_t;

/**
 * @brief Negotiation result structure
 */
typedef struct httpd_content_negotiation_result {
    char *selected_media_type;       /**< Best matching media type */
    char *selected_encoding;         /**< Best matching encoding */
    char *selected_language;         /**< Best matching language */
    char *selected_charset;          /**< Best matching charset */

    // For generating response headers
    bool vary_header_needed;         /**< Whether Vary header should be set */
    char *vary_header_value;         /**< Vary header field list */

    // Internal scoring for debugging
    float media_type_score;          /**< Quality score for media type selection */
    float encoding_score;           /**< Quality score for encoding selection */
} httpd_content_negotiation_result_t;

/**
 * @brief Content negotiation middleware configuration
 */
typedef struct httpd_content_negotiation_config {
    httpd_content_capabilities_t capabilities;  /**< Server capabilities */

    // Optional callback to dynamically change capabilities per request
    esp_err_t (*get_capabilities)(httpd_req_t *req,
                                httpd_content_capabilities_t *capabilities);

    // Context for custom operations
    void *context;
    httpd_free_ctx_fn_t free_ctx;

    // Response modification callbacks
    esp_err_t (*set_content_type)(httpd_req_t *req, const char *content_type);
    esp_err_t (*add_vary_header)(httpd_req_t *req, const char *vary_value);

    // For testing (dependency injection)
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field,
                                      char *val, size_t val_size);
} httpd_content_negotiation_config_t;

/**
 * @brief Parse Accept header with quality values
 *
 * @param header_value Raw Accept header value
 * @return Parsed accept ranges or NULL on error
 */
httpd_accept_range_t* httpd_parse_accept_header(const char *header_value);

/**
 * @brief Free accept ranges structure
 */
void httpd_free_accept_ranges(httpd_accept_range_t *ranges);

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
                                httpd_content_negotiation_result_t *result);

/**
 * @brief Convenience function for simple media type negotiation
 */
esp_err_t httpd_negotiate_media_type(const char *accept_header,
                                   char **available_types,
                                   char **selected_type);

/**
 * @brief Content negotiation middleware
 *
 * Parses request Accept headers and performs content negotiation,
 * storing results in request user context for handlers to use.
 */
esp_err_t middleware_content_negotiation(httpd_req_t *req,
                                       const httpd_uri_t *uri,
                                       void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_CONTENT_NEGOTIATION_H_ */
