/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#endif

#ifdef ESP_PLATFORM
#include <esp_err.h>
#include <netinet/tcp.h>
#endif

#include "esp_httpd_priv.h"
#include <http_server.h>

#include <log.h>
#include "httpd_chunked.h"
#include "httpd_connection.h"

#define HTTPD_ERR_CHUNK_SIZE_INVALID 0x1000

static const char *TAG = "httpd_txrx";

/**
 * Check if a string contains CRLF sequences that could enable HTTP response splitting
 * @param str String to check
 * @return true if CRLF sequences are found (either raw \r\n or URL-encoded %0D%0A)
 */
static bool httpd_contains_crlf(const char *str) {
    if (!str) return false;

    const char *ptr = str;
    while (*ptr) {
        // Check for raw CRLF sequence
        if (*ptr == '\r' && *(ptr + 1) == '\n') {
            return true;
        }
        // Check for URL-encoded CRLF sequence %0D%0A
        if (*ptr == '%' && *(ptr + 1) == '0' && *(ptr + 2) == 'D' &&
            *(ptr + 3) == '%' && *(ptr + 4) == '0' && *(ptr + 5) == 'A') {
            return true;
        }
        ptr++;
    }
    return false;
}

esp_err_t httpd_sess_set_send_override(httpd_handle_t hd, int sockfd, httpd_send_func_t send_func)
{
    struct sock_db *sess = httpd_sess_get(hd, sockfd);
    if (!sess) {
        return ESP_ERR_INVALID_ARG;
    }
    sess->send_fn = send_func;
    return ESP_OK;
}

esp_err_t httpd_sess_set_recv_override(httpd_handle_t hd, int sockfd, httpd_recv_func_t recv_func)
{
    struct sock_db *sess = httpd_sess_get(hd, sockfd);
    if (!sess) {
        return ESP_ERR_INVALID_ARG;
    }
    sess->recv_fn = recv_func;
    return ESP_OK;
}

esp_err_t httpd_sess_set_pending_override(httpd_handle_t hd, int sockfd, httpd_pending_func_t pending_func)
{
    struct sock_db *sess = httpd_sess_get(hd, sockfd);
    if (!sess) {
        return ESP_ERR_INVALID_ARG;
    }
    sess->pending_fn = pending_func;
    return ESP_OK;
}

int httpd_send(httpd_req_t *r, const char *buf, size_t buf_len)
{
    if (r == NULL || buf == NULL) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    if (!httpd_valid_req(r)) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    struct httpd_req_aux *ra = r->aux;
    int ret = ra->sd->send_fn(ra->sd->handle, ra->sd->fd, buf, buf_len, 0);
    if (ret < 0) {
        // log_write(4, TAG,  
        //     "D (%u) %s: %s:%d [%s] %s: error in send_fn\033[0m\n", 
        //     log_timestamp(), 
        //     TAG, 
        //     "H:\\LiluSoft\\Projects\\ESP32SecurityCamera\\POC6WebServer\\AsyncWebServer\\lib\\esp_http_server\\src\\httpd_txrx.c", 
        //     62, 
        //     __FUNCTION__
        // );

        LOGD(TAG, LOG_FMT("error in send_fn"));
        return ret;
    }
    return ret;
}

static esp_err_t httpd_send_all(httpd_req_t *r, const char *buf, size_t buf_len)
{
    struct httpd_req_aux *ra = r->aux;
    int ret;

    while (buf_len > 0) {
        ret = ra->sd->send_fn(ra->sd->handle, ra->sd->fd, buf, buf_len, 0);
        if (ret < 0) {
            LOGD(TAG, LOG_FMT("error in send_fn"));
            #ifdef _WIN32
            LOGD(TAG, LOG_FMT("send error: %d"), WSAGetLastError());
            #else
            LOGD(TAG, LOG_FMT("send error: %d"), errno);
            #endif
            return ESP_FAIL;
        }
        LOGD(TAG, LOG_FMT("sent = %d"), ret);
        buf     += ret;
        buf_len -= ret;
    }
    return ESP_OK;
}

static size_t httpd_recv_pending(httpd_req_t *r, char *buf, size_t buf_len)
{
    struct httpd_req_aux *ra = r->aux;
    size_t offset = sizeof(ra->sd->pending_data) - ra->sd->pending_len;

    /* buf_len must not be greater than remaining_len */
    buf_len = MIN(ra->sd->pending_len, buf_len);
    memcpy(buf, ra->sd->pending_data + offset, buf_len);

    ra->sd->pending_len -= buf_len;
    return buf_len;
}

int httpd_recv_with_opt(httpd_req_t *r, char *buf, size_t buf_len, bool halt_after_pending)
{
    LOGD(TAG, LOG_FMT("requested length = %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST(buf_len));

    size_t pending_len = 0;
    struct httpd_req_aux *ra = r->aux;

    /* First fetch pending data from local buffer */
    if (ra->sd->pending_len > 0) {
        LOGD(TAG, LOG_FMT("pending length = %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST(ra->sd->pending_len));
        pending_len = httpd_recv_pending(r, buf, buf_len);
        buf     += pending_len;
        buf_len -= pending_len;

        /* If buffer filled then no need to recv.
         * If asked to halt after receiving pending data then
         * return with received length */
        if (!buf_len || halt_after_pending) {
            return pending_len;
        }
    }

    /* Receive data of remaining length */
    int ret = ra->sd->recv_fn(ra->sd->handle, ra->sd->fd, buf, buf_len, 0);
    if (ret < 0) {
        LOGD(TAG, LOG_FMT("error in recv_fn"));
        if ((ret == HTTPD_SOCK_ERR_TIMEOUT) && (pending_len != 0)) {
            /* If recv() timeout occurred, but pending data is
             * present, return length of pending data.
             * This behavior is similar to that of socket recv()
             * function, which, in case has only partially read the
             * requested length, due to timeout, returns with read
             * length, rather than error */
            return pending_len;
        }
        return ret;
    }

    LOGD(TAG, LOG_FMT("received length = %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST((ret + pending_len)));
    return ret + pending_len;
}

int httpd_recv(httpd_req_t *r, char *buf, size_t buf_len)
{
    return httpd_recv_with_opt(r, buf, buf_len, false);
}

size_t httpd_unrecv(struct httpd_req *r, const char *buf, size_t buf_len)
{
    struct httpd_req_aux *ra = r->aux;
    /* Truncate if external buf_len is greater than pending_data buffer size */
    ra->sd->pending_len = MIN(sizeof(ra->sd->pending_data), buf_len);

    /* Copy data into internal pending_data buffer with the exact offset
     * such that it is right aligned inside the buffer */
    size_t offset = sizeof(ra->sd->pending_data) - ra->sd->pending_len;
    memcpy(ra->sd->pending_data + offset, buf, ra->sd->pending_len);
    LOGD(TAG, LOG_FMT("length = %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST(ra->sd->pending_len));
    return ra->sd->pending_len;
}

/**
 * This API appends an additional header field-value pair in the HTTP response.
 * But the header isn't sent out until any of the send APIs is executed.
 */
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value)
{
    if (r == NULL || field == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Security check: prevent CRLF injection for response splitting attacks */
    if (httpd_contains_crlf(field) || httpd_contains_crlf(value)) {
        LOGW(TAG, LOG_FMT("CRLF injection attempt in header: %s"), field);
        return ESP_ERR_INVALID_ARG;
    }

    if (!httpd_valid_req(r)) {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    struct httpd_req_aux *ra = r->aux;
    struct httpd_data *hd = (struct httpd_data *) r->handle;

    /* Check if header already exists and overwrite if so */
    for (unsigned i = 0; i < ra->resp_hdrs_count; i++) {
        if (strcmp(ra->resp_hdrs[i].field, field) == 0) {
            /* Free existing allocations */
            free(ra->resp_hdrs[i].field);
            free(ra->resp_hdrs[i].value);
            /* Duplicate new field and value */
            ra->resp_hdrs[i].field = strdup(field);
            ra->resp_hdrs[i].value = strdup(value);
            if (!ra->resp_hdrs[i].field || !ra->resp_hdrs[i].value) {
                /* Allocation failed, free anything that was allocated */
                free(ra->resp_hdrs[i].field);
                free(ra->resp_hdrs[i].value);
                ra->resp_hdrs[i].field = NULL;
                ra->resp_hdrs[i].value = NULL;
                return ESP_ERR_NO_MEM;
            }
            LOGD(TAG, LOG_FMT("updated header = %s: %s"), field, value);
            return ESP_OK;
        }
    }

    /* Number of additional headers is limited */
    if (ra->resp_hdrs_count >= hd->config.max_resp_headers) {
        return ESP_ERR_HTTPD_RESP_HDR;
    }

    /* Duplicate field and value strings */
    ra->resp_hdrs[ra->resp_hdrs_count].field = strdup(field);
    ra->resp_hdrs[ra->resp_hdrs_count].value = strdup(value);
    if (!ra->resp_hdrs[ra->resp_hdrs_count].field || !ra->resp_hdrs[ra->resp_hdrs_count].value) {
        /* Allocation failed, free anything that was allocated */
        free(ra->resp_hdrs[ra->resp_hdrs_count].field);
        free(ra->resp_hdrs[ra->resp_hdrs_count].value);
        ra->resp_hdrs[ra->resp_hdrs_count].field = NULL;
        ra->resp_hdrs[ra->resp_hdrs_count].value = NULL;
        return ESP_ERR_NO_MEM;
    }
    ra->resp_hdrs_count++;

    LOGD(TAG, LOG_FMT("new header = %s: %s"), field, value);
    return ESP_OK;
}

/**
 * This API sets the status of the HTTP response to the value specified.
 * But the status isn't sent out until any of the send APIs is executed.
 */
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{
    if (r == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!httpd_valid_req(r)) {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    struct httpd_req_aux *ra = r->aux;

    /* Security check: prevent CRLF injection for response splitting attacks */
    if (httpd_contains_crlf(status)) {
        LOGW(TAG, LOG_FMT("CRLF injection attempt in status line"));
        return ESP_ERR_INVALID_ARG;
    }

    ra->status = (char *)status;
    return ESP_OK;
}

/**
 * This API sets the method/type of the HTTP response to the value specified.
 * But the method isn't sent out until any of the send APIs is executed.
 */
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    if (r == NULL || type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!httpd_valid_req(r)) {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    struct httpd_req_aux *ra = r->aux;
    ra->content_type = (char *)type;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len)
{
    if (r == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!httpd_valid_req(r)) {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    /* GUARD RAIL: Prevent double response sending which causes HTTP stream corruption
     * Added in response to Range middleware double-send bug
     * See: test_e2e_range_valid_open_ended_range failure in normal execution
     */
    if (r->response_sent) {
        LOGE(TAG, LOG_FMT("Attempted to send response when one was already sent"));
        return ESP_ERR_INVALID_STATE;
    }
    r->response_sent = true;

    struct httpd_req_aux *ra = r->aux;
    const char *httpd_hdr_str = "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zd\r\n";
    const char *colon_separator = ": ";
    const char *cr_lf_separator = "\r\n";

    if (buf_len == HTTPD_RESP_USE_STRLEN) {
        buf_len = strlen(buf);
    }

    /* Request headers are no longer available */
    ra->req_hdrs_count = 0;

    /* Automatically add Connection header when connection will not persist after response */
    bool should_add_connection_header = false;
    const char *connection_value = NULL;

    if (!httpd_connection_should_persist(r->handle, ra->sd->fd)) {
        should_add_connection_header = true;
        connection_value = "close";
        LOGD(TAG, "Adding Connection: close header for fd=%d", ra->sd->fd);
    }

    /* Check if Connection header is already manually set */
    bool connection_header_exists = false;
    if (should_add_connection_header) {
        for (unsigned i = 0; i < ra->resp_hdrs_count; i++) {
            if (strcasecmp(ra->resp_hdrs[i].field, "Connection") == 0) {
                connection_header_exists = true;
                break;
            }
        }
    }

    /* Add Connection header if needed and not already present */
    if (should_add_connection_header && !connection_header_exists) {
        esp_err_t hdr_err = httpd_resp_set_hdr(r, "Connection", connection_value);
        if (hdr_err != ESP_OK) {
            LOGW(TAG, "Failed to add Connection header: %d", hdr_err);
            // Continue anyway, as this shouldn't prevent response sending
        }
    }

    /* Size of essential headers is limited by scratch buffer size */
    if (snprintf(ra->scratch, sizeof(ra->scratch), httpd_hdr_str,
                 ra->status, ra->content_type, buf_len) >= sizeof(ra->scratch)) {
        return ESP_ERR_HTTPD_RESP_HDR;
    }

    /* Sending essential headers */
    if (httpd_send_all(r, ra->scratch, strlen(ra->scratch)) != ESP_OK) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }

    /* Sending additional headers based on set_header */
    for (unsigned i = 0; i < ra->resp_hdrs_count; i++) {
        /* Send header field */
        if (httpd_send_all(r, ra->resp_hdrs[i].field, strlen(ra->resp_hdrs[i].field)) != ESP_OK) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        /* Send ': ' */
        if (httpd_send_all(r, colon_separator, strlen(colon_separator)) != ESP_OK) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        /* Send header value */
        if (httpd_send_all(r, ra->resp_hdrs[i].value, strlen(ra->resp_hdrs[i].value)) != ESP_OK) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        /* Send CR + LF */
        if (httpd_send_all(r, cr_lf_separator, strlen(cr_lf_separator)) != ESP_OK) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }

    /* End header section */
    if (httpd_send_all(r, cr_lf_separator, strlen(cr_lf_separator)) != ESP_OK) {
        return ESP_ERR_HTTPD_RESP_SEND;
    }
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_HEADERS_SENT, &(ra->sd->fd), sizeof(int));

    /* Free response headers memory allocations after sending headers */
    httpd_resp_hdrs_free(ra);

    /* Sending content */
    if (buf && buf_len) {
        if (httpd_send_all(r, buf, buf_len) != ESP_OK) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
    }
    esp_http_server_event_data evt_data = {
        .fd = ra->sd->fd,
        .data_len = buf_len,
    };
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_SENT_DATA, &evt_data, sizeof(esp_http_server_event_data));
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len)
{
    if (r == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!httpd_valid_req(r)) {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    if (buf_len == HTTPD_RESP_USE_STRLEN) {
        buf_len = strlen(buf);
    }

    struct httpd_req_aux *ra = r->aux;

    /* Request headers are no longer available */
    ra->req_hdrs_count = 0;

    if (!ra->first_chunk_sent) {
        // Start chunked response (sends headers)
        esp_err_t err = httpd_start_chunked_response(r, ra->content_type, NULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    // Send chunk (no extensions for backward compat)
    httpd_chunk_extensions_t no_exts = {0};
    return httpd_send_chunk(r, buf, buf_len, &no_exts);
}

esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *usr_msg)
{
    esp_err_t ret;
    const char *msg;
    const char *status;

    switch (error) {
    case HTTPD_501_METHOD_NOT_IMPLEMENTED:
        status = "501 Method Not Implemented";
        msg    = "Server does not support this method";
        break;
    case HTTPD_505_VERSION_NOT_SUPPORTED:
        status = "505 Version Not Supported";
        msg    = "HTTP version not supported by server";
        break;
    case HTTPD_400_BAD_REQUEST:
        status = "400 Bad Request";
        msg    = "Bad request syntax";
        break;
    case HTTPD_401_UNAUTHORIZED:
        status = "401 Unauthorized";
        msg    = "No permission -- see authorization schemes";
        break;
    case HTTPD_403_FORBIDDEN:
        status = "403 Forbidden";
        msg    = "Request forbidden -- authorization will not help";
        break;
    case HTTPD_404_NOT_FOUND:
        status = "404 Not Found";
        msg    = "Nothing matches the given URI";
        break;
    case HTTPD_405_METHOD_NOT_ALLOWED:
        status = "405 Method Not Allowed";
        msg    = "Specified method is invalid for this resource";
        break;
    case HTTPD_408_REQ_TIMEOUT:
        status = "408 Request Timeout";
        msg    = "Server closed this connection";
        break;
    case HTTPD_414_URI_TOO_LONG:
        status = "414 URI Too Long";
        msg    = "URI is too long";
        break;
    case HTTPD_411_LENGTH_REQUIRED:
        status = "411 Length Required";
        msg    = "Client must specify Content-Length";
        break;
    case HTTPD_413_CONTENT_TOO_LARGE:
        status = "413 Content Too Large";
        msg    = "Content is too large";
        break;
    case HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE:
        status = "431 Request Header Fields Too Large";
        msg    = "Header fields are too long";
        break;
    case HTTPD_500_INTERNAL_SERVER_ERROR:
    default:
        status = "500 Internal Server Error";
        msg    = "Server has encountered an unexpected error";
    }

    /* If user has provided custom message, override default message */
    msg = usr_msg ? usr_msg : msg;
    LOGW(TAG, LOG_FMT("%s - %s"), status, msg);

    /* Set error code in HTTP response */
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* Use TCP_NODELAY option to force socket to send data in buffer
     * This ensures that the error message is sent before the socket
     * is closed */
    struct httpd_req_aux *ra = req->aux;
    int nodelay = 1;
    if (setsockopt(ra->sd->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        /* If failed to turn on TCP_NODELAY, throw warning and continue */
        LOGW(TAG, LOG_FMT("error calling setsockopt : %d"), errno);
        nodelay = 0;
    }
#endif

    /* Send HTTP error message */
    ret = httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* If TCP_NODELAY was set successfully above, time to disable it */
    if (nodelay == 1) {
        nodelay = 0;
        if (setsockopt(ra->sd->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            /* If failed to turn off TCP_NODELAY, throw error and
             * return failure to signal for socket closure */
            LOGE(TAG, LOG_FMT("error calling setsockopt : %d"), errno);
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_ERROR, &error, sizeof(httpd_err_code_t));

    return ret;
}

esp_err_t httpd_resp_send_custom_err(httpd_req_t *req, const char *status, const char *msg)
{
    LOGW(TAG, LOG_FMT("%s - %s"), status, msg);

    /* Set error code in HTTP response */
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* Use TCP_NODELAY option to force socket to send data in buffer
     * This ensures that the error message is sent before the socket
     * is closed */
    struct httpd_req_aux *ra = req->aux;
    int nodelay = 1;
    if (setsockopt(ra->sd->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        /* If failed to turn on TCP_NODELAY, throw warning and continue */
        LOGW(TAG, LOG_FMT("error calling setsockopt : %d"), errno);
        nodelay = 0;
    }
#endif

    /* Send HTTP error message */
    esp_err_t ret = httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* If TCP_NODELAY was set successfully above, time to disable it */
    if (nodelay == 1) {
        nodelay = 0;
        if (setsockopt(ra->sd->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            /* If failed to turn off TCP_NODELAY, throw error and
             * return failure to signal for socket closure */
            LOGE(TAG, LOG_FMT("error calling setsockopt : %d"), errno);
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif
    return ret;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle,
                                     httpd_err_code_t error,
                                     httpd_err_handler_func_t err_handler_fn)
{
    if (handle == NULL || error >= HTTPD_ERR_CODE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    struct httpd_data *hd = (struct httpd_data *) handle;
    hd->err_handler_fns[error] = err_handler_fn;
    return ESP_OK;
}

esp_err_t httpd_req_handle_err(httpd_req_t *req, httpd_err_code_t error)
{
    struct httpd_data *hd = (struct httpd_data *) req->handle;
    esp_err_t ret;

    /* Invoke custom error handler if configured */
    if (hd->err_handler_fns[error]) {
        ret = hd->err_handler_fns[error](req, error);

        /* If error code is 500, force return failure
         * irrespective of the handler's return value */
        ret = (error == HTTPD_500_INTERNAL_SERVER_ERROR ? ESP_FAIL : ret);
    } else {
        /* If no handler is registered for this error default
         * behavior is to send the HTTP error response and
         * return failure for closure of underlying socket */
        httpd_resp_send_err(req, error, NULL);
        ret = ESP_FAIL;
    }
    return ret;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)
{
    if (r == NULL || buf == NULL) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    if (!httpd_valid_req(r)) {
        LOGW(TAG, LOG_FMT("invalid request"));
        return HTTPD_SOCK_ERR_INVALID;
    }

    struct httpd_req_aux *ra = r->aux;
    LOGD(TAG, LOG_FMT("remaining length = %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST(ra->remaining_len));

    size_t bytes_read = 0;
    if (ra->chunk_ctx) {
        // Chunked encoding: use chunked read
        esp_err_t err = httpd_read_chunk(r, ra->chunk_ctx, buf, buf_len, &bytes_read);
        if (err == ESP_OK) {
            // Chunk data read successfully
        } else if (err == ESP_ERR_NOT_FOUND) {
            // Final chunk reached, no more data
            bytes_read = 0;
            ra->remaining_len = 0; // Signal end
        } else if (err == HTTPD_ERR_CHUNK_SIZE_INVALID) {
            // Invalid chunk size, likely due to EOF after all data consumed
            bytes_read = 0;
            ra->remaining_len = 0;
            return 0;
        } else {
            // Error in chunk reading
            LOGD(TAG, LOG_FMT("chunk read error: %d"), err);
            return HTTPD_SOCK_ERR_FAIL;
        }
    } else {
        // Normal Content-Length
        if (buf_len > ra->remaining_len) {
            buf_len = ra->remaining_len;
        }
        if (buf_len == 0) {
            return 0;
        }

        int ret = httpd_recv(r, buf, buf_len);
        if (ret < 0) {
            LOGD(TAG, LOG_FMT("error in httpd_recv (%d)"), ret);
            return ret;
        }
        bytes_read = ret;
        ra->remaining_len -= bytes_read;
    }

    LOGD(TAG, LOG_FMT("received length = %zu"), bytes_read);
    esp_http_server_event_data evt_data = {
        .fd = ra->sd->fd,
        .data_len = bytes_read,
    };
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_ON_DATA, &evt_data, sizeof(esp_http_server_event_data));
    return bytes_read;
}

esp_err_t httpd_req_async_handler_begin(httpd_req_t *r, httpd_req_t **out)
{
    if (r == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // alloc async req
    httpd_req_t *async = malloc(sizeof(httpd_req_t));
    if (async == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(async, r, sizeof(httpd_req_t));

    // alloc async aux
    async->aux = malloc(sizeof(struct httpd_req_aux));
    if (async->aux == NULL) {
        free(async);
        return ESP_ERR_NO_MEM;
    }
    memcpy(async->aux, r->aux, sizeof(struct httpd_req_aux));

    // Copy response header block
    struct httpd_data *hd = (struct httpd_data *) r->handle;
    struct httpd_req_aux *async_aux = (struct httpd_req_aux *) async->aux;
    struct httpd_req_aux *r_aux = (struct httpd_req_aux *) r->aux;

    async_aux->resp_hdrs = calloc(hd->config.max_resp_headers, sizeof(struct resp_hdr));
    if (async_aux->resp_hdrs == NULL) {
        free(async_aux);
        free(async);
        return ESP_ERR_NO_MEM;
    }

    // Deep copy response headers with string duplication
    for (unsigned i = 0; i < r_aux->resp_hdrs_count; i++) {
        async_aux->resp_hdrs[i].field = r_aux->resp_hdrs[i].field ? strdup(r_aux->resp_hdrs[i].field) : NULL;
        async_aux->resp_hdrs[i].value = r_aux->resp_hdrs[i].value ? strdup(r_aux->resp_hdrs[i].value) : NULL;
        if ((r_aux->resp_hdrs[i].field && !async_aux->resp_hdrs[i].field) ||
            (r_aux->resp_hdrs[i].value && !async_aux->resp_hdrs[i].value)) {
            // Free any allocated strings on failure
            for (unsigned j = 0; j <= i; j++) {
                free(async_aux->resp_hdrs[j].field);
                free(async_aux->resp_hdrs[j].value);
            }
            free(async_aux->resp_hdrs);
            free(async_aux);
            free(async);
            return ESP_ERR_NO_MEM;
        }
    }
    async_aux->resp_hdrs_count = r_aux->resp_hdrs_count;

    // Prevent the main thread from reading the rest of the request after the handler returns.
    r_aux->remaining_len = 0;

    // mark socket as "in use"
    r_aux->sd->for_async_req = true;

    *out = async;

    return ESP_OK;
}

esp_err_t httpd_req_async_handler_complete(httpd_req_t *r)
{
    if (r == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct httpd_req_aux *ra = r->aux;
    ra->sd->for_async_req = false;

    // Check if session should be closed after async completion
    if (ra->sd->close_after_async_complete) {
        httpd_sess_trigger_close(r->handle, ra->sd->fd);
    }

    /* Free response headers memory allocations */
    httpd_resp_hdrs_free(ra);
    free(ra->resp_hdrs);
    free(r->aux);
    free(r);

    return ESP_OK;
}

int httpd_req_to_sockfd(httpd_req_t *r)
{
    if (r == NULL) {
        return -1;
    }

    if (!httpd_valid_req(r)) {
        LOGW(TAG, LOG_FMT("invalid request"));
        return -1;
    }

    struct httpd_req_aux *ra = r->aux;
    return ra->sd->fd;
}

static int httpd_sock_err(const char *ctx, int sockfd)
{
    int errval;
#ifdef _WIN32
    int err = WSAGetLastError();
    LOGW(TAG, LOG_FMT("error in %s : %d"), ctx, err);

    switch (err) {
    case WSAEWOULDBLOCK:
    case WSAEINTR:
    case WSAETIMEDOUT:
        errval = HTTPD_SOCK_ERR_TIMEOUT;
        break;
    case WSAEINVAL:
    case WSAEBADF:
    case WSAEFAULT:
    case WSAENOTSOCK:
        errval = HTTPD_SOCK_ERR_INVALID;
        break;
    default:
        errval = HTTPD_SOCK_ERR_FAIL;
    }
#else
    LOGW(TAG, LOG_FMT("error in %s : %d"), ctx, errno);

    switch (errno) {
    case EAGAIN:
    case EINTR:
        errval = HTTPD_SOCK_ERR_TIMEOUT;
        break;
    case EINVAL:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
        errval = HTTPD_SOCK_ERR_INVALID;
        break;
    default:
        errval = HTTPD_SOCK_ERR_FAIL;
    }
#endif
    return errval;
}

int httpd_default_send(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags)
{
    (void)hd;
    if (buf == NULL) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    int ret;
#ifdef _WIN32
    ret = send(sockfd, buf, buf_len, flags);
#else
    ret = send(sockfd, buf, buf_len, flags | MSG_NOSIGNAL);
#endif
    if (ret < 0) {
        return httpd_sock_err("send", sockfd);
    }
    return ret;
}

int httpd_default_recv(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags)
{
    (void)hd;
    if (buf == NULL) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    int ret = recv(sockfd, buf, buf_len, flags);
    if (ret < 0) {
        return httpd_sock_err("recv", sockfd);
    }
    return ret;
}

int httpd_socket_send(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags)
{
    struct sock_db *sess = httpd_sess_get(hd, sockfd);
    if (!sess) {
        return HTTPD_SOCK_ERR_INVALID;
    }
    if (!sess->send_fn) {
        return HTTPD_SOCK_ERR_INVALID;
    }
    return sess->send_fn(hd, sockfd, buf, buf_len, flags);
}

int httpd_socket_recv(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags)
{
    struct sock_db *sess = httpd_sess_get(hd, sockfd);
    if (!sess) {
        return HTTPD_SOCK_ERR_INVALID;
    }
    if (!sess->recv_fn) {
        return HTTPD_SOCK_ERR_INVALID;
    }
    return sess->recv_fn(hd, sockfd, buf, buf_len, flags);
}

/**
 * @brief   Free response headers memory allocations
 */
void httpd_resp_hdrs_free(struct httpd_req_aux *ra)
{
    for (unsigned i = 0; i < ra->resp_hdrs_count; i++) {
        free(ra->resp_hdrs[i].field);
        ra->resp_hdrs[i].field = NULL;
        free(ra->resp_hdrs[i].value);
        ra->resp_hdrs[i].value = NULL;
    }
    ra->resp_hdrs_count = 0;
}
