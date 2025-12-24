/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HTTPD_CHUNKED_H
#define HTTPD_CHUNKED_H


#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../src/port/events.h"
#include <limits.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_event_base.h>
#else

#endif


#ifdef __cplusplus
extern "C" {
#endif

// #include <esp_err.h>

typedef struct httpd_req httpd_req_t;

/**
 * @brief Chunked transfer encoding context
 */
typedef struct {
    size_t chunk_size;           /**< Current chunk size */
    size_t chunk_received;       /**< Bytes received in current chunk */
    size_t total_received;       /**< Total bytes received */
    bool chunk_size_known;       /**< Whether chunk size is parsed */
    bool final_chunk;            /**< Whether final chunk is received */
    bool has_extensions;         /**< Whether chunk has extensions */
    char *extensions;            /**< Chunk extensions string */
    size_t extensions_len;       /**< Length of extensions */
    char *trailer_buffer;        /**< Buffer for trailer headers */
    size_t trailer_buffer_size;  /**< Size of trailer buffer */
    size_t trailer_buffer_used;  /**< Used bytes in trailer buffer */
    size_t max_chunk_size;       /**< Maximum allowed chunk size */
    size_t max_chunks;           /**< Maximum allowed chunks */
    size_t max_trailer_size;     /**< Maximum trailer size */
    size_t chunk_count;          /**< Number of chunks processed */
    bool enable_chunk_extensions;/**< Enable chunk extensions */
    bool enable_trailers;        /**< Enable trailer sections */
} httpd_chunked_ctx_t;

/**
 * @brief Transfer encoding specific error codes
 */
typedef enum {
    HTTPD_ERR_CHUNK_SIZE_INVALID = 0x1000,     /**< Invalid chunk size */
    HTTPD_ERR_CHUNK_SIZE_TOO_LARGE,            /**< Chunk size exceeds limit */
    HTTPD_ERR_CHUNK_COUNT_EXCEEDED,            /**< Too many chunks */
    HTTPD_ERR_CHUNK_CRLF_MISSING,              /**< Missing CRLF in chunk */
    HTTPD_ERR_CHUNK_DATA_TRUNCATED,            /**< Truncated chunk data */
    HTTPD_ERR_TRAILER_TOO_LARGE,               /**< Trailer exceeds size limit */
    HTTPD_ERR_EXTENSION_INVALID,               /**< Invalid chunk extension */
    HTTPD_ERR_TRANSFER_CODING_UNSUPPORTED,     /**< Unsupported transfer coding */
    HTTPD_ERR_MALFORMED_CHUNKED_DATA           /**< Malformed chunked data */
} httpd_transfer_error_t;

/**
 * @brief Transfer coding configuration
 */
typedef struct {
    bool enable_chunked_encoding;    /**< Enable chunked transfer encoding */
    bool enable_chunk_extensions;    /**< Enable chunk extensions */
    bool enable_trailers;            /**< Enable trailer sections */
    size_t max_chunk_size;           /**< Maximum chunk size (bytes) */
    size_t max_chunks;               /**< Maximum number of chunks */
    size_t max_trailer_size;         /**< Maximum trailer size (bytes) */
    bool strict_validation;          /**< Enable strict RFC validation */
} httpd_transfer_config_t;

/**
 * @brief Chunk extension parameter
 */
typedef struct {
    char *name;                    /**< Parameter name */
    char *value;                   /**< Parameter value */
    size_t name_len;               /**< Name length */
    size_t value_len;              /**< Value length */
} httpd_chunk_ext_param_t;

/**
 * @brief Parsed chunk extensions
 */
typedef struct {
    httpd_chunk_ext_param_t *params;  /**< Array of parameters */
    size_t param_count;               /**< Number of parameters */
    size_t param_capacity;            /**< Capacity of params array */
} httpd_chunk_extensions_t;

/* Default configuration */
extern const httpd_transfer_config_t httpd_transfer_default_config;

/**
 * @brief Parse chunked transfer encoding from request
 */
esp_err_t httpd_parse_chunked_request(httpd_req_t *req, httpd_chunked_ctx_t *chunked_ctx);

/**
 * @brief Read next chunk from request
 */
esp_err_t httpd_read_chunk(httpd_req_t *req, httpd_chunked_ctx_t *chunked_ctx,
                          void *buffer, size_t buffer_size, size_t *bytes_read);

/**
 * @brief Parse chunk extensions
 */
esp_err_t httpd_parse_chunk_extensions(const char *extensions_str,
                                     httpd_chunk_extensions_t *extensions);

/**
 * @brief Parse trailer headers
 */
esp_err_t httpd_parse_trailers(httpd_req_t *req, httpd_chunked_ctx_t *chunked_ctx);

/**
 * @brief Initialize chunked response
 */
esp_err_t httpd_start_chunked_response(httpd_req_t *req, const char *content_type,
                                     const httpd_transfer_config_t *transfer_config);

/**
 * @brief Send chunk of data
 */
esp_err_t httpd_send_chunk(httpd_req_t *req, const void *data, size_t data_size,
                          const httpd_chunk_extensions_t *extensions);

/**
 * @brief Send final chunk and trailers
 */
esp_err_t httpd_end_chunked_response(httpd_req_t *req, const char *trailers);

#ifdef __cplusplus
}
#endif

#endif /* !HTTPD_CHUNKED_H */
