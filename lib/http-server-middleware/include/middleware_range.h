#ifndef _MIDDLEWARE_RANGE_H_
#define _MIDDLEWARE_RANGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>  // For ssize_t

#include <http_server.h>  // For httpd_req_t and related types

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum range specifications allowed in a single request (DoS protection)
 */
#define MAX_RANGE_SPECS 10

/**
 * @brief Range specification structure representing a single byte range
 */
typedef struct httpd_range_spec {
    long long start;        /**< Starting byte position (inclusive) */
    long long end;          /**< Ending byte position (inclusive, -1 for open-ended) */
    bool has_start;         /**< Whether start position is specified */
    bool has_end;           /**< Whether end position is specified */
} httpd_range_spec_t;

/**
 * @brief Parsed range request from Range header
 */
typedef struct httpd_range_request {
    bool is_valid;              /**< Whether the range request is valid */
    char *range_unit;           /**< Range unit (usually "bytes") */
    size_t range_count;         /**< Number of range specifications */
    httpd_range_spec_t *ranges; /**< Array of range specifications */
    long long total_length;     /**< Total content length known at parse time (-1 if unknown) */
} httpd_range_request_t;

/**
 * @brief Context for range response generation
 */
typedef struct httpd_range_response_ctx {
    const httpd_range_request_t *request;  /**< Parsed range request */
    long long content_length;              /**< Total content length */
    const char *content_type;              /**< Content type for response */
    void *user_ctx;                        /**< User context for handler */
} httpd_range_response_ctx_t;

/**
 * @brief Range handler function type
 *
 * This function is called when a valid range request is detected.
 * It should handle the range request and send appropriate partial content response.
 *
 * @param req HTTP request structure
 * @param ctx Range response context
 * @return ESP_OK on success, error code on failure
 */
typedef esp_err_t (*httpd_range_handler_t)(httpd_req_t *req,
                                         const httpd_range_response_ctx_t *ctx);

/**
 * @brief Range requests middleware configuration
 */
typedef struct httpd_range_middleware_config {
    httpd_range_handler_t handler;      /**< Range request handler function */
    void *context;                      /**< User context for handler */
    httpd_free_ctx_fn_t free_ctx;       /**< Context cleanup function */
    const char *content_type;           /**< MIME type for range responses */
    long long content_length;           /**< Total content length (-1 if dynamic) */
    bool enable_multiple_ranges;        /**< Whether to support multiple ranges (future extension) */

    // Function callbacks for dependency injection (for testing)
    bool (*req_has_range_header)(httpd_req_t *req);                                                 /**< Check if range header exists callback */
    httpd_range_request_t* (*req_parse_range_header)(httpd_req_t *req, long long content_length);   /**< Parse range header callback */
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req, const char *field, char *val, size_t val_size); /**< Get header value callback */
    size_t (*req_get_hdr_value_len)(httpd_req_t *req, const char *field);                               /**< Get header value length callback */
    esp_err_t (*resp_send_range_not_satisfiable)(httpd_req_t *req, long long total_length);             /**< Send 416 response callback */
    esp_err_t (*resp_set_status)(httpd_req_t *req, const char *status);                                 /**< Set response status callback */
    esp_err_t (*resp_set_hdr)(httpd_req_t *req, const char *field, const char *value);                  /**< Set response header callback */
    esp_err_t (*resp_send)(httpd_req_t *req, const char *buf, long buf_len);                             /**< Send response callback */
} httpd_range_middleware_config_t;

/**
 * @brief Check if a request has a Range header
 *
 * @param req HTTP request structure
 * @return true if Range header is present, false otherwise
 */
bool httpd_req_has_range_header(httpd_req_t *req);

/**
 * @brief Parse Range header from request
 *
 * Caller is responsible for freeing the returned structure with httpd_range_free()
 *
 * @param req HTTP request structure
 * @param config Configuration with callbacks and content length
 * @return Parsed range request structure, or NULL on error
 */
httpd_range_request_t* httpd_parse_range_header(httpd_req_t *req, const httpd_range_middleware_config_t *config);

/**
 * @brief Free range request structure
 *
 * @param range_req Structure to free
 */
void httpd_range_free(httpd_range_request_t *range_req);

/**
 * @brief Send partial content response with Content-Range header
 *
 * @param req HTTP request structure
 * @param data Partial content data
 * @param data_len Length of content data
 * @param range_start Starting byte position of this range
 * @param range_end Ending byte position of this range
 * @param total_length Total content length
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_resp_send_partial_content(httpd_req_t *req,
                                        const char *data,
                                        size_t data_len,
                                        long long range_start,
                                        long long range_end,
                                        long long total_length);

/**
 * @brief Send 416 Range Not Satisfiable response
 *
 * @param req HTTP request structure
 * @param total_length Total content length (-1 if unknown)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_resp_send_range_not_satisfiable(httpd_req_t *req, long long total_length);

/**
 * @brief Range requests middleware function
 *
 * This function checks for Range headers and calls the configured handler
 * if a valid range request is present. If no Range header is present,
 * it returns ESP_OK to continue processing.
 *
 * @param req HTTP request structure
 * @param uri URI handler being invoked
 * @param ctx Middleware context (httpd_range_middleware_config_t*)
 * @return ESP_OK to continue, ESP_FAIL to stop processing
 */
esp_err_t middleware_range(httpd_req_t *req, const httpd_uri_t *uri, void *ctx);

/**
 * @brief Set test range header value (for unit testing)
 *
 * @param value Range header string to set for testing, or NULL to clear
 */
void httpd_set_test_range_header(const char *value);

#ifdef __cplusplus
}
#endif

#endif /* _MIDDLEWARE_RANGE_H_ */
