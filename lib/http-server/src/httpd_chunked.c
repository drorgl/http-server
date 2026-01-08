/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "httpd_chunked.h"
#include "esp_httpd_priv.h"
#include "http_server.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TAG "httpd_chunked"

/* Default transfer encoding configuration */
const httpd_transfer_config_t httpd_transfer_default_config = {
    .enable_chunked_encoding = true,
    .enable_chunk_extensions = true,
    .enable_trailers = true,
    .max_chunk_size = 1024 * 1024,    // 1MB
    .max_chunks = 1000,
    .max_trailer_size = 8192,         // 8KB
    .strict_validation = true
};

/**
 * @brief Parse hex chunk size from line with overflow protection
 */
static esp_err_t parse_chunk_size(const char *line, size_t *size_out, size_t max_size) {
    size_t size = 0;
    bool valid_hex = true;
    const char *orig_line = line;
    while (*line && *line != ';') {
        uint8_t digit;
        if (isdigit((unsigned char)*line)) {
            digit = *line - '0';
        } else if (isxdigit((unsigned char)*line)) {
            digit = tolower(*line) - 'a' + 10;
        } else {
            valid_hex = false;
            break;
        }
        // Overflow check
        if (size > (SIZE_MAX - digit) / 16) {
            return HTTPD_ERR_CHUNK_SIZE_TOO_LARGE;
        }
        size = size * 16 + digit;
        line++;
    }
    if (!valid_hex || size > max_size || (line == orig_line)) {
        return HTTPD_ERR_CHUNK_SIZE_INVALID;
    }
    *size_out = size;
    return ESP_OK;
}

/**
 * @brief Validate chunk boundaries for security
 */
bool httpd_validate_chunk_boundaries(const httpd_chunked_ctx_t *ctx) {
    if (ctx->chunk_size > ctx->max_chunk_size ||
        ctx->chunk_count > ctx->max_chunks ||
        ctx->total_received > ctx->max_chunk_size * ctx->max_chunks) {  // Total limit
        return false;
    }
    return true;
}

esp_err_t httpd_parse_chunked_request(httpd_req_t *req, httpd_chunked_ctx_t *ctx) {
    if (!ctx || ctx->chunk_size_known) {
        return ESP_ERR_INVALID_STATE;
    }
    struct httpd_data *hd = (struct httpd_data *)req->handle;
    const httpd_transfer_config_t *cfg = &hd->config.transfer_cfg;
    ctx->max_chunk_size = cfg->max_chunk_size;
    ctx->max_chunks = cfg->max_chunks;
    ctx->max_trailer_size = cfg->max_trailer_size;
    ctx->enable_chunk_extensions = cfg->enable_chunk_extensions;
    ctx->enable_trailers = cfg->enable_trailers;
    ctx->chunk_size_known = false;
    ctx->final_chunk = false;
    ctx->chunk_received = 0;
    ctx->total_received = 0;
    ctx->chunk_count = 0;
    return ESP_OK;
}

esp_err_t httpd_read_chunk(httpd_req_t *req, httpd_chunked_ctx_t *ctx,
                          void *buffer, size_t buffer_size, size_t *bytes_read) {
    *bytes_read = 0;
    if (ctx->final_chunk) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!ctx->chunk_size_known) {
        // Read chunk header line byte-by-byte to avoid overreading past \r\n
        char line[128];
        int line_idx = 0;
        bool crlf_consumed = false;
        while (line_idx < sizeof(line) - 1) {
            char c;
            int ret = httpd_recv(req, &c, 1);
            if (ret <= 0) return ret;
            if (c == '\r') {
                // Check if followed by \n (consumes it)
                int peek_ret = httpd_recv(req, &c, 1);
                if (peek_ret <= 0) {
                    line[line_idx++] = '\r';
                } else if (c == '\n') {
                    // Found CRLF
                    crlf_consumed = true;
                    break;
                } else {
                    // \r not followed by \n, treat as data
                    line[line_idx++] = '\r';
                    if (line_idx < sizeof(line) - 1) {
                        line[line_idx++] = c;
                    }
                }
            } else {
                line[line_idx++] = c;
            }
        }
        line[line_idx] = '\0';
        LOGD(TAG, "Chunk line: '%s' consumed_crlf: %d", line, crlf_consumed);
        // Find end of size ( ; for extensions)
        char *ext_start = strchr(line, ';');
        size_t hex_len = ext_start ? (ext_start - line) : strcspn(line, "\r;\t ");
        line[hex_len] = '\0'; // Temp null terminate for parse
        // Parse size
        esp_err_t err = parse_chunk_size(line, &ctx->chunk_size, ctx->max_chunk_size);
        if (ext_start) line[hex_len] = *ext_start; // Restore only if ext_start exists
        if (err != ESP_OK) {
            LOGD(TAG, "Chunk size parse error: %d, line: '%s'", err, line);
            return err;
        }

        // Extensions
        if (ctx->has_extensions && ctx->enable_chunk_extensions) {
            httpd_chunk_extensions_t exts;
            err = httpd_parse_chunk_extensions(ext_start, &exts);
            if (err == ESP_OK) {
                // TODO: process exts
                // Free exts
                for (size_t i = 0; i < exts.param_count; i++) {
                    free(exts.params[i].name);
                    free(exts.params[i].value);
                }
                free(exts.params);
            }
        }

        // Validate boundaries
        if (!httpd_validate_chunk_boundaries(ctx)) {
            return HTTPD_ERR_CHUNK_COUNT_EXCEEDED;
        }
        ctx->chunk_size_known = true;
        ctx->chunk_received = 0;
        ctx->has_extensions = (ext_start != NULL);
        // Skip CRLF (low level)
        if (!crlf_consumed) {
            char crlf[2];
            if (httpd_recv(req, crlf, 2) != 2 || crlf[0] != '\r' || crlf[1] != '\n') {
                return HTTPD_ERR_CHUNK_CRLF_MISSING;
            }
        }
        ctx->chunk_count++;
        if (ctx->chunk_count > ctx->max_chunks) {
            return HTTPD_ERR_CHUNK_COUNT_EXCEEDED;
        }
        if (ctx->chunk_size == 0) {
            ctx->final_chunk = true;
            if (ctx->enable_trailers) {
                esp_err_t trailer_err = httpd_parse_trailers(req, ctx);
                if (trailer_err != ESP_OK) {
                    return trailer_err;
                }
            }
            return ESP_ERR_NOT_FOUND;
        }
    }
    size_t to_read = MIN(buffer_size, ctx->chunk_size - ctx->chunk_received);
    int len = httpd_recv(req, buffer, to_read);
    if (len < 0) return len;
    ctx->chunk_received += len;
    ctx->total_received += len;
    *bytes_read = len;
    if (ctx->chunk_received == ctx->chunk_size) {
        ctx->chunk_size_known = false;
        // Skip CRLF (low level)
        char crlf[2];
        if (httpd_recv(req, crlf, 2) != 2 || crlf[0] != '\r' || crlf[1] != '\n') {
            return HTTPD_ERR_CHUNK_CRLF_MISSING;
        }
    }
    return ESP_OK;
}

static char *httpd_strndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static bool httpd_contains_crlf(const char *s) {
    if (!s) return false;
    size_t len = strlen(s);
    const char *p = s;
    while (len > 1) {
        if (p[0] == '\r' && p[1] == '\n') return true;
        p++;
        len--;
    }
    return false;
}

esp_err_t httpd_parse_chunk_extensions(const char *extensions_str,
                                     httpd_chunk_extensions_t *extensions) {
    if (!extensions_str || !extensions) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize
    extensions->param_count = 0;
    extensions->param_capacity = 8; // Initial capacity
    extensions->params = calloc(extensions->param_capacity, sizeof(httpd_chunk_ext_param_t));
    if (!extensions->params) {
        return ESP_ERR_NO_MEM;
    }

    const char *ptr = extensions_str;
    while (*ptr) {
        // Skip whitespace
        while (*ptr == ' ') ptr++;

        if (*ptr == ';') {
            ptr++; // Skip ;
            // Parse param name
            const char *name_start = ptr;
            while (*ptr && *ptr != '=' && *ptr != ';') ptr++;
            size_t name_len = ptr - name_start;

            httpd_chunk_ext_param_t *param = NULL;
            if (extensions->param_count >= extensions->param_capacity) {
                // Grow array
                extensions->param_capacity *= 2;
                httpd_chunk_ext_param_t *new_params = realloc(extensions->params,
                    extensions->param_capacity * sizeof(httpd_chunk_ext_param_t));
                if (!new_params) {
                    // Free existing
                    for (size_t i = 0; i < extensions->param_count; i++) {
                        free(extensions->params[i].name);
                        free(extensions->params[i].value);
                    }
                    free(extensions->params);
                    return ESP_ERR_NO_MEM;
                }
                extensions->params = new_params;
            }
            param = &extensions->params[extensions->param_count];

            param->name = httpd_strndup(name_start, name_len);
            param->name_len = name_len;
            param->value = NULL;
            param->value_len = 0;

            if (*ptr == '=') {
                ptr++; // Skip =
                // Parse value (quoted or token)
                const char *value_start = ptr;
                bool quoted = (*ptr == '"');
                if (quoted) ptr++; // Skip opening "
                while (*ptr && (quoted ? (*ptr != '"') : (*ptr != ';' && *ptr != ' '))) ptr++;
                size_t value_len = ptr - value_start;
                if (quoted && *ptr == '"') ptr++; // Skip closing "
                param->value = httpd_strndup(value_start, value_len);
                param->value_len = value_len;
            }
            extensions->param_count++;
        } else {
            ptr++;
        }
    }

    // Trim capacity
    if (extensions->param_count < extensions->param_capacity) {
        httpd_chunk_ext_param_t *trimmed = realloc(extensions->params,
            extensions->param_count * sizeof(httpd_chunk_ext_param_t));
        if (trimmed) extensions->params = trimmed;
    }

    return ESP_OK;
}

esp_err_t httpd_parse_trailers(httpd_req_t *req, httpd_chunked_ctx_t *ctx) {
    if (!ctx || ctx->final_chunk == false) {
        return ESP_ERR_INVALID_STATE;
    }

    // Trailers are optional headers after final chunk
    // Reuse header parsing logic or simple parse
    // For now, allocate trailer_buffer and read until empty line
    if (!ctx->trailer_buffer) {
        ctx->trailer_buffer_size = ctx->max_trailer_size;
        ctx->trailer_buffer = malloc(ctx->trailer_buffer_size);
        if (!ctx->trailer_buffer) {
            return ESP_ERR_NO_MEM;
        }
        ctx->trailer_buffer_used = 0;
    }

    // Read until empty line or limit exceeded
    char line[256];
    while (ctx->trailer_buffer_used < ctx->trailer_buffer_size) {
        int len = httpd_recv(req, line, sizeof(line) - 1);
        if (len <= 0) break;
        line[len] = '\0';

        if (len == 2 && line[0] == '\r' && line[1] == '\n') {
            // Empty line - end of trailers
            break;
        }

        // Validate trailer header value for security before appending
        // Extract value part after colon, excluding leading spaces and CRLF
        const char *colon = strchr(line, ':');
        if (colon) {
            const char *value_start = colon + 1;
            // Skip leading spaces
            while (*value_start == ' ') value_start++;
            // Find end before CRLF (len includes \r\n, but may not if line truncated)
            char *value_end = line + len;
            if (len >= 2 && line[len-2] == '\r' && line[len-1] == '\n') {
                value_end -= 2;
            }
            // Null terminate temporarily for extraction
            char temp_end = *value_end;
            *value_end = '\0';
            if (httpd_contains_crlf(value_start)) {
                // Security violation - CRLF in trailer value
                if (ctx->trailer_buffer) {
                    free(ctx->trailer_buffer);
                    ctx->trailer_buffer = NULL;
                }
                return ESP_ERR_INVALID_ARG;
            }
            // Restore
            *value_end = temp_end;
        }

        // Copy to buffer
        size_t copy_len = MIN(len, ctx->trailer_buffer_size - ctx->trailer_buffer_used);
        memcpy(ctx->trailer_buffer + ctx->trailer_buffer_used, line, copy_len);
        ctx->trailer_buffer_used += copy_len;
        if (ctx->trailer_buffer_used >= ctx->trailer_buffer_size) {
            return HTTPD_ERR_TRAILER_TOO_LARGE;
        }
    }

    if (ctx->trailer_buffer_used > ctx->max_trailer_size) {
        return HTTPD_ERR_TRAILER_TOO_LARGE;
    }

    LOGD(TAG, "Parsed trailers (%zu bytes)", ctx->trailer_buffer_used);
    return ESP_OK;
}

esp_err_t httpd_start_chunked_response(httpd_req_t *req, const char *content_type,
                                     const httpd_transfer_config_t *transfer_config) {
    if (!req || httpd_valid_req(req) == false) {
        return ESP_ERR_INVALID_ARG;
    }

    struct httpd_req_aux *ra = (struct httpd_req_aux *)req->aux;
    struct httpd_data *hd = (struct httpd_data *)req->handle;

    // Use default config if not provided
    const httpd_transfer_config_t *cfg = transfer_config ? transfer_config : &httpd_transfer_default_config;
    if (!cfg->enable_chunked_encoding) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Set content type
    httpd_resp_set_type(req, content_type);

    // Prepare headers: HTTP/1.1 + status + Content-Type + Transfer-Encoding + additional headers + \r\n
    char *hdr_start = ra->scratch;
    int hdr_len = snprintf(hdr_start, sizeof(ra->scratch),
                           "HTTP/1.1 %s\r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\n",
                           ra->status ? ra->status : "200 OK", content_type);
    if (hdr_len < 0 || hdr_len >= sizeof(ra->scratch)) {
        return ESP_ERR_HTTPD_RESP_HDR;
    }

    // Add additional response headers
    const char *colon = ": ";
    const char *crlf = "\r\n";
    for (unsigned i = 0; i < ra->resp_hdrs_count && hdr_len < sizeof(ra->scratch) - 128; i++) {
        int add_len = snprintf(hdr_start + hdr_len, sizeof(ra->scratch) - hdr_len,
                               "%s%s%s%s", ra->resp_hdrs[i].field, colon,
                               ra->resp_hdrs[i].value, crlf);
        if (add_len < 0) break;
        hdr_len += add_len;
    }

    // Final CRLF
    if (hdr_len < sizeof(ra->scratch) - 2) {
        strcpy(hdr_start + hdr_len, crlf);
        hdr_len += 2;
    }

    // Send headers
    if (httpd_send(req, hdr_start, hdr_len) != hdr_len) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    // Clear response headers
    httpd_resp_hdrs_free(ra);

    // Flag chunked response active
    ra->first_chunk_sent = true;  // Reuse for chunked responses

    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_HEADERS_SENT, &(ra->sd->fd), sizeof(int));
    return ESP_OK;
}

esp_err_t httpd_send_chunk(httpd_req_t *req, const void *data, size_t data_size,
                          const httpd_chunk_extensions_t *extensions) {
    struct httpd_req_aux *ra = (struct httpd_req_aux *)req->aux;
    if (!req || httpd_valid_req(req) == false || !ra->first_chunk_sent) {
        return ESP_ERR_INVALID_STATE;
    }
    const httpd_transfer_config_t *cfg = &httpd_transfer_default_config;

    // Chunk size limit
    if (data_size > cfg->max_chunk_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Format chunk header
    char header[32];
    int header_len;
    if (extensions && extensions->param_count > 0) {
        // Append extensions ";key=value;..."
        char ext_str[128] = {0};
        for (size_t i = 0; i < extensions->param_count && strlen(ext_str) < 100; i++) {
            httpd_chunk_ext_param_t *p = &extensions->params[i];
            size_t ext_len = strlen(ext_str);
            if (p->name) {
                snprintf(ext_str + ext_len, sizeof(ext_str) - ext_len, ";%.*s", (int)p->name_len, p->name);
                ext_len = strlen(ext_str);
                if (p->value) {
                    snprintf(ext_str + ext_len, sizeof(ext_str) - ext_len, "=%.*s", (int)p->value_len, p->value);
                }
            }
        }
        header_len = snprintf(header, sizeof(header), "%zx%s\r\n", data_size, ext_str);
    } else {
        header_len = snprintf(header, sizeof(header), "%zx\r\n", data_size);
    }

    if (header_len < 0 || header_len >= sizeof(header)) {
        return ESP_ERR_HTTPD_RESP_HDR;
    }

    // Send header
    if (httpd_send(req, header, header_len) != header_len) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    // Send data
    if (data && data_size > 0) {
        if (httpd_send(req, data, data_size) != (int)data_size) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }

    // Send CRLF
    if (httpd_send(req, "\r\n", 2) != 2) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    esp_http_server_event_data evt_data = {
        .fd = ra->sd->fd,
        .data_len = data_size,
    };
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_SENT_DATA, &evt_data, sizeof(evt_data));
    return ESP_OK;
}

esp_err_t httpd_end_chunked_response(httpd_req_t *req, const char *trailers) {
    struct httpd_req_aux *ra = (struct httpd_req_aux *)req->aux;
    if (!req || httpd_valid_req(req) == false || !ra->first_chunk_sent) {
        return ESP_ERR_INVALID_STATE;
    }

    // Send final 0 chunk
    const char *zero_chunk = "0\r\n";
    if (httpd_send(req, zero_chunk, 3) != 3) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    // Send trailers if provided
    if (trailers && *trailers) {
        // Validate trailers length/security
        size_t trailers_len = strlen(trailers);
        if (trailers_len > httpd_transfer_default_config.max_trailer_size ||
            httpd_contains_crlf(trailers)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (httpd_send(req, trailers, trailers_len) != (int)trailers_len) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }

    // Final CRLF
    if (httpd_send(req, "\r\n", 2) != 2) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    // Reset chunked flag
    ra->first_chunk_sent = false;

    return ESP_OK;
}
