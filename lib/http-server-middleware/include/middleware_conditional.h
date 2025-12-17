/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MIDDLEWARE_CONDITIONAL_H_
#define _MIDDLEWARE_CONDITIONAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>  // For ssize_t
#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length of ETag value
 */
#define HTTPD_MAX_ETAG_LEN 128

/**
 * @brief ETag generation function type
 *
 * This function is called to generate an ETag for a resource.
 *
 * @param req HTTP request structure
 * @param etag Buffer to store the generated ETag
 * @param etag_len Length of the etag buffer
 * @return ESP_OK on success, error code on failure
 */
typedef esp_err_t (*httpd_etag_generator_t)(httpd_req_t *req, char *etag, size_t etag_len);

/**
 * @brief Last-Modified timestamp function type
 *
 * This function is called to get the last modification timestamp of a resource.
 *
 * @param req HTTP request structure
 * @param last_modified Pointer to store the last modification timestamp
 * @return ESP_OK on success, error code on failure
 */
typedef esp_err_t (*httpd_last_modified_fn_t)(httpd_req_t *req, long long *last_modified);

/**
 * @brief Conditional requests middleware configuration
 */
typedef struct httpd_conditional_middleware_config {
    httpd_etag_generator_t etag_generator;          /**< ETag generation function */
    httpd_last_modified_fn_t last_modified_fn;     /**< Last-Modified timestamp function */
    void *context;                                 /**< User context for functions */
    httpd_free_ctx_fn_t free_ctx;                  /**< Context cleanup function */

    // Function callbacks for dependency injection (for testing)
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size);
    size_t (*req_get_hdr_value_len)(httpd_req_t *req, const char *field);
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);
    esp_err_t (*resp_send)(httpd_req_t *req, const char *buf, ssize_t buf_len);
} httpd_conditional_middleware_config_t;

/**
 * @brief Conditional requests middleware function
 *
 * This function checks for conditional request headers (If-Match, If-None-Match,
 * If-Modified-Since, If-Unmodified-Since) and handles them according to RFC 9110.
 *
 * @param req HTTP request structure
 * @param uri URI handler being invoked
 * @param ctx Middleware context (httpd_conditional_middleware_config_t*)
 * @return ESP_OK to continue, ESP_FAIL to stop processing
 */
esp_err_t middleware_conditional(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

/**
 * @brief Generate a strong ETag from content
 *
 * Creates a strong ETag in the format "content_hash" where content_hash
 * is a hash of the content.
 *
 * @param content Pointer to content data
 * @param content_len Length of content data
 * @param etag Buffer to store the generated ETag
 * @param etag_len Length of the etag buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_generate_strong_etag(const char *content, size_t content_len, char *etag, size_t etag_len);

/**
 * @brief Generate a weak ETag from timestamp
 *
 * Creates a weak ETag in the format W/"timestamp" where timestamp
 * represents the last modification time.
 *
 * @param timestamp Last modification timestamp
 * @param etag Buffer to store the generated ETag
 * @param etag_len Length of the etag buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_generate_weak_etag(long long timestamp, char *etag, size_t etag_len);

/**
 * @brief Parse HTTP date string to timestamp
 *
 * Parses HTTP date formats (RFC 7231) to a timestamp.
 *
 * @param date_str Date string to parse
 * @param timestamp Pointer to store the parsed timestamp
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_parse_http_date(const char *date_str, long long *timestamp);

/**
 * @brief Format timestamp to HTTP date string
 *
 * Formats a timestamp to HTTP date format (RFC 7231).
 *
 * @param timestamp Timestamp to format
 * @param date_str Buffer to store the formatted date string
 * @param date_len Length of the date_str buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_format_http_date(long long timestamp, char *date_str, size_t date_len);

/**
 * @brief If-Range condition structure
 */
typedef struct httpd_if_range_condition {
    bool is_etag;                       /**< true if ETag, false if HTTP-date */
    char etag[HTTPD_MAX_ETAG_LEN];      /**< ETag value (if is_etag=true) */
    long long http_date;                /**< HTTP-date timestamp (if is_etag=false) */
} httpd_if_range_condition_t;

/**
 * @brief If-Range precondition evaluation result
 */
typedef enum {
    HTTPD_IF_RANGE_PROCESS_RANGE,      /**< Proceed with range request processing */
    HTTPD_IF_RANGE_IGNORE_RANGE        /**< Ignore Range header, return full content */
} httpd_if_range_result_t;

/**
 * @brief Parse If-Range header value
 *
 * Parses an If-Range header value into an if_range_condition_t structure.
 *
 * @param header_value If-Range header value to parse
 * @param condition Pointer to condition structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_parse_if_range_header(const char *header_value,
                                     httpd_if_range_condition_t *condition);

/**
 * @brief Evaluate If-Range precondition
 *
 * Evaluates whether a range request should proceed based on If-Range condition.
 *
 * @param req HTTP request structure
 * @param resource_etag Resource ETag for comparison
 * @param resource_last_modified Resource last modification timestamp
 * @param if_range Parsed If-Range condition
 * @return HTTPD_IF_RANGE_PROCESS_RANGE or HTTPD_IF_RANGE_IGNORE_RANGE
 */
httpd_if_range_result_t httpd_evaluate_if_range_precondition(httpd_req_t *req,
                                                           const char *resource_etag,
                                                           long long resource_last_modified,
                                                           const httpd_if_range_condition_t *if_range);

/**
 * @brief Set test conditional header values (for unit testing)
 *
 * @param header_name Header name to set
 * @param header_value Header value to set, or NULL to clear
 */
void httpd_set_test_conditional_header(const char *header_name, const char *header_value);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_CONDITIONAL_H_ */
