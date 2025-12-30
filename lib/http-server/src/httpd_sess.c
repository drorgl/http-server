/*
 * SPDX-FileCopyrightText: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>

#include <errno.h>
#include <malloc.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#endif

#ifdef ESP_PLATFORM
#include <stdlib.h>
#include <esp_err.h>
#include <unistd.h>

#include <http_server.h>
#endif

#include "esp_httpd_priv.h"
#include "httpd_connection.h"
#include <log.h>



static const char *TAG = "httpd_sess";

typedef enum {
    HTTPD_TASK_NONE = 0,
    HTTPD_TASK_INIT,            // Init session
    HTTPD_TASK_GET_ACTIVE,      // Get active session (fd!=-1)
    HTTPD_TASK_GET_FREE,        // Get free session slot (fd<0)
    HTTPD_TASK_FIND_FD,         // Find session with specific fd
    HTTPD_TASK_SET_DESCRIPTOR,  // Set descriptor
    HTTPD_TASK_DELETE_INVALID,  // Delete invalid session
    HTTPD_TASK_FIND_LOWEST_LRU, // Find session with lowest lru
    HTTPD_TASK_CLOSE            // Close session
} task_t;

typedef struct {
    task_t task;
    int fd;
    fd_set *fdset;
    int max_fd;
    struct httpd_data *hd;
    uint64_t lru_counter;
    struct sock_db    *session;
} enum_context_t;

void httpd_sess_enum(struct httpd_data *hd, httpd_session_enum_function enum_function, void *context)
{
    if ((!hd) || (!hd->hd_sd) || (!hd->config.max_open_sockets)) {
        return;
    }

    struct sock_db *current = hd->hd_sd;
    struct sock_db *end = hd->hd_sd + hd->config.max_open_sockets - 1;

    while (current <= end) {
        if (enum_function && (!enum_function(current, context))) {
            break;
        }
        current++;
    }
}

// Check if a FD is valid
static int fd_is_valid(int fd)
{
    int result;
#ifdef _WIN32
    // 1. Check for the general INVALID_SOCKET value (like -1 on Unix)
    if (fd == (int)INVALID_SOCKET) {
        LOGD(TAG, "fd_is_valid: fd=%d, result=0 (INVALID_SOCKET)", fd);
        return 0; // Not valid
    }

    // 2. Attempt a non-destructive socket operation
    int error = 0;
    int len = sizeof(error);

    // getsockopt will fail on an invalid socket handle.
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == SOCKET_ERROR)
    {
        // On failure, check the specific Windows Sockets error code.
        int wsa_error = WSAGetLastError();

        // WSAENOTSOCK (10038) is the Winsock error for "Socket operation on non-socket,"
        // which is the closest Windows equivalent to EBADF (Bad File Descriptor).
        if (wsa_error == WSAENOTSOCK) {
            LOGD(TAG, "fd_is_valid: fd=%d, result=0 (WSAENOTSOCK=%d)", fd, wsa_error);
            return 0; // Definitely not a socket/valid file descriptor
        }

        // If it fails for another reason, the socket *handle* itself might still be valid,
        // but the socket might be in an error state (which SO_ERROR would report).
        // Since we are checking handle validity, we return true for other errors.
        result = 1;
    } else {
        result = 1;
    }

    LOGD(TAG, "fd_is_valid: fd=%d, result=%d", fd, result);
    return result;
#else
    result = fcntl(fd, F_GETFD) != -1 || errno != EBADF;
    LOGD(TAG, "fd_is_valid: fd=%d, result=%d, errno=%d", fd, result, errno);
    return result;
#endif
}

static int enum_function(struct sock_db *session, void *context)
{
    if ((!session) || (!context)) {
        return 0;
    }
    enum_context_t *ctx = (enum_context_t *) context;
    int found = 0;
    switch (ctx->task) {
    // Initialize session
    case HTTPD_TASK_INIT:
        session->fd = -1;
        session->ctx = NULL;
        session->connection_ctx = NULL;
        session->for_async_req = false;
        session->close_after_async_complete = false;
        break;
    // Get active session
    case HTTPD_TASK_GET_ACTIVE:
        found = (session->fd != -1);
        break;
    // Get free slot
    case HTTPD_TASK_GET_FREE:
        found = (session->fd < 0);
        break;
    // Find fd
    case HTTPD_TASK_FIND_FD:
        if (ctx->fd < 0) { // If searching for an invalid FD, don't find anything
            found = 0;
        } else {
            found = (session->fd == ctx->fd);
        }
        break;
    // Set descriptor
    case HTTPD_TASK_SET_DESCRIPTOR:
        if (session->fd != -1 && !session->for_async_req) {
            if (fd_is_valid(session->fd)) {
                FD_SET(session->fd, ctx->fdset);
                if (session->fd > ctx->max_fd) {
                    ctx->max_fd = session->fd;
                }
            } else {
                LOGW(TAG, "Skipping invalid fd %d in set_descriptors", session->fd);
            }
        }
        break;
    // Delete invalid session - FIXED: Only check sockets that were previously valid (>= 0)
    case HTTPD_TASK_DELETE_INVALID:
        if (session->fd >= 0 && !fd_is_valid(session->fd)) {
            LOGW(TAG, LOG_FMT("Closing invalid socket %d"), session->fd);
            httpd_sess_delete(ctx->hd, session);
        }
        break;
    // Find lowest lru
    case HTTPD_TASK_FIND_LOWEST_LRU:
        // Found free slot - no need to check other sessions
        if (session->fd == -1) {
            return 0;
        }
        // Only close sockets that are not in use
        if (session->for_async_req == false) {
            // Check/update lowest lru
            if (session->lru_counter < ctx->lru_counter) {
                ctx->lru_counter = session->lru_counter;
                ctx->session = session;
            }
        }
        break;
    case HTTPD_TASK_CLOSE:
        if (session->fd != -1) {
            LOGD(TAG, LOG_FMT("cleaning up socket %d"), session->fd);
            httpd_sess_delete(ctx->hd, session);
        }
        break;
    default:
        return 0;
    }
    if (found) {
        ctx->session = session;
        return 0;
    }
    return 1;
}

static void httpd_sess_close(void *arg)
{
    struct sock_db *sock_db = (struct sock_db *) arg;
    if (!sock_db) {
        return;
    }

    if (!sock_db->lru_counter && !sock_db->lru_socket) {
        LOGD(TAG, "Skipping session close for %d as it seems to be a race condition", sock_db->fd);
        return;
    }
    sock_db->lru_socket = false;
    struct httpd_data *hd = (struct httpd_data *) sock_db->handle;
    httpd_sess_delete(hd, sock_db);
}

struct sock_db *httpd_sess_get_free(struct httpd_data *hd)
{
    if ((!hd) || (hd->hd_sd_active_count == hd->config.max_open_sockets)) {
        return NULL;
    }
    enum_context_t context = {
        .task = HTTPD_TASK_GET_FREE
    };
    httpd_sess_enum(hd, enum_function, &context);
    return context.session;
}

bool httpd_is_sess_available(struct httpd_data *hd)
{
    return httpd_sess_get_free(hd) ? true : false;
}

struct sock_db *httpd_sess_get(struct httpd_data *hd, int sockfd)
{
    if ((!hd) || (!hd->hd_sd) || (!hd->config.max_open_sockets)) {
        return NULL;
    }

    // Check if called inside a request handler, and the session sockfd in use is same as the parameter
    // => Just return the pointer to the sock_db corresponding to the request
    if ((hd->hd_req_aux.sd) && (hd->hd_req_aux.sd->fd == sockfd)) {
        return hd->hd_req_aux.sd;
    }

    enum_context_t context = {
        .task = HTTPD_TASK_FIND_FD,
        .fd = sockfd
    };
    httpd_sess_enum(hd, enum_function, &context);
    return context.session;
}

esp_err_t httpd_sess_new(struct httpd_data *hd, int newfd)
{
    LOGD(TAG, LOG_FMT("fd = %d"), newfd);

    if (httpd_sess_get(hd, newfd)) {
        LOGE(TAG, LOG_FMT("session already exists with fd = %d"), newfd);
        return ESP_FAIL;
    }

    struct sock_db *session = httpd_sess_get_free(hd);
    if (!session) {
        LOGD(TAG, LOG_FMT("unable to launch session for fd = %d"), newfd);
        return ESP_FAIL;
    }

    // Clear session data
    memset(session, 0, sizeof (struct sock_db));
    session->fd = newfd;
    session->handle = (httpd_handle_t) hd;
    session->send_fn = httpd_default_send;
    session->recv_fn = httpd_default_recv;

    // increment number of sessions
    hd->hd_sd_active_count++;

    // Call user-defined session opening function
    if (hd->config.open_fn) {
        esp_err_t ret = hd->config.open_fn(hd, session->fd);
        if (ret != ESP_OK) {
            httpd_sess_delete(hd, session);
            LOGD(TAG, LOG_FMT("open_fn failed for fd = %d"), newfd);
            return ret;
        }
    }

    // Initialize HTTP/1.1 connection persistence context
    if (httpd_connection_init(hd, session->fd) != ESP_OK) {
        LOGE(TAG, LOG_FMT("connection context initialization failed for fd = %d"), newfd);
        httpd_sess_delete(hd, session);
        return ESP_FAIL;
    }

    LOGD(TAG, LOG_FMT("active sockets: %d"), hd->hd_sd_active_count);
    return ESP_OK;
}

void httpd_sess_free_ctx(void **ctx, httpd_free_ctx_fn_t free_fn)
{
    if ((!ctx) || (!*ctx)) {
        return;
    }
    if (free_fn) {
        free_fn(*ctx);
    } else {
        free(*ctx);
    }
    *ctx = NULL;
}

void httpd_sess_clear_ctx(struct sock_db *session)
{
    if ((!session) || ((!session->ctx) && (!session->transport_ctx) && (!session->connection_ctx))) {
        return;
    }

    // free user ctx
    if (session->ctx) {
        httpd_sess_free_ctx(&session->ctx, session->free_ctx);
        session->free_ctx = NULL;
    }

    // Free connection context
    if (session->connection_ctx) {
        httpd_sess_free_ctx((void**)&session->connection_ctx, session->free_connection_ctx);
        session->free_connection_ctx = NULL;
    }

    // Free 'transport' context
    if (session->transport_ctx) {
        httpd_sess_free_ctx(&session->transport_ctx, session->free_transport_ctx);
        session->free_transport_ctx = NULL;
    }
}

void *httpd_sess_get_ctx(httpd_handle_t handle, int sockfd)
{
    struct sock_db *session = httpd_sess_get(handle, sockfd);
    if (!session) {
        return NULL;
    }

    // Check if the function has been called from inside a
    // request handler, in which case fetch the context from
    // the httpd_req_t structure
    struct httpd_data *hd = (struct httpd_data *) handle;
    if (hd->hd_req_aux.sd == session) {
        return hd->hd_req.sess_ctx;
    }
    return session->ctx;
}

void httpd_sess_set_ctx(httpd_handle_t handle, int sockfd, void *ctx, httpd_free_ctx_fn_t free_fn)
{
    struct sock_db *session = httpd_sess_get(handle, sockfd);
    if (!session) {
        return;
    }

    // Check if the function has been called from inside a
    // request handler, in which case set the context inside
    // the httpd_req_t structure
    struct httpd_data *hd = (struct httpd_data *) handle;
    if (hd->hd_req_aux.sd == session) {
        if (hd->hd_req.sess_ctx != ctx) {
            // Don't free previous context if it is in sockdb
            // as it will be freed inside httpd_req_cleanup()
            if (session->ctx != hd->hd_req.sess_ctx) {
                httpd_sess_free_ctx(&hd->hd_req.sess_ctx, hd->hd_req.free_ctx); // Free previous context
            }
            hd->hd_req.sess_ctx = ctx;
        }
        hd->hd_req.free_ctx = free_fn;
        return;
    }

    // Else set the context inside the sock_db structure
    if (session->ctx != ctx) {
        // Free previous context
        httpd_sess_free_ctx(&session->ctx, session->free_ctx);
        session->ctx = ctx;
    }
    session->free_ctx = free_fn;
}

void *httpd_sess_get_transport_ctx(httpd_handle_t handle, int sockfd)
{
    struct sock_db *session = httpd_sess_get(handle, sockfd);
    return session ? session->transport_ctx : NULL;
}

void httpd_sess_set_transport_ctx(httpd_handle_t handle, int sockfd, void *ctx, httpd_free_ctx_fn_t free_fn)
{
    struct sock_db *session = httpd_sess_get(handle, sockfd);
    if (!session) {
        return;
    }

    if (session->transport_ctx != ctx) {
        // Free previous transport context
        httpd_sess_free_ctx(&session->transport_ctx, session->free_transport_ctx);
        session->transport_ctx = ctx;
    }
    session->free_transport_ctx = free_fn;
}

void httpd_sess_set_descriptors(struct httpd_data *hd, fd_set *fdset, int *maxfd)
{
    enum_context_t context = {
        .task = HTTPD_TASK_SET_DESCRIPTOR,
        .max_fd = -1,
        .fdset = fdset
    };
    httpd_sess_enum(hd, enum_function, &context);
    if (maxfd) {
        *maxfd = context.max_fd;
    }
}

void httpd_sess_delete_invalid(struct httpd_data *hd)
{
    LOGD(TAG, "httpd_sess_delete_invalid: entry, active_count=%u", hd->hd_sd_active_count);
    enum_context_t context = {
        .task = HTTPD_TASK_DELETE_INVALID,
        .hd = hd
    };
    httpd_sess_enum(hd, enum_function, &context);
    LOGD(TAG, "httpd_sess_delete_invalid: exit, active_count=%u", hd->hd_sd_active_count);
}

void httpd_sess_delete(struct httpd_data *hd, struct sock_db *session)
{
    if ((!hd) || (!session) || (session->fd < 0)) {
        return;
    }

    LOGD(TAG, LOG_FMT("fd = %d"), session->fd);
    if (hd->config.enable_so_linger) {
        struct linger so_linger = {
            .l_onoff = true,
            .l_linger = hd->config.linger_timeout,
        };
        if (setsockopt(session->fd, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(struct linger)) < 0) {
            LOGW(TAG, LOG_FMT("error enabling SO_LINGER (%d)"), errno);
        }
    }

    // Call close function if defined
    if (hd->config.close_fn) {
        hd->config.close_fn(hd, session->fd);
    } else {
        close(session->fd);
    }
    esp_http_server_dispatch_event(HTTP_SERVER_EVENT_DISCONNECTED, &session->fd, sizeof(int));

    // clear all contexts
    httpd_sess_clear_ctx(session);

    // mark session slot as available
    session->fd = -1;

    // decrement number of sessions
    hd->hd_sd_active_count--;
    LOGD(TAG, LOG_FMT("active sockets: %d"), hd->hd_sd_active_count);
    if (!hd->hd_sd_active_count) {
        hd->lru_counter = 0;
    }
}

void httpd_sess_init(struct httpd_data *hd)
{
    enum_context_t context = {
        .task = HTTPD_TASK_INIT
    };
    httpd_sess_enum(hd, enum_function, &context);
}

bool httpd_sess_pending(struct httpd_data *hd, struct sock_db *session)
{
    if (!session) {
        return false;
    }
    if (session->pending_fn) {
        // test if there's any data to be read (besides read() function, which is handled by select() in the main httpd loop)
        // this should check e.g. for the SSL data buffer
        if (session->pending_fn(hd, session->fd) > 0) {
            return true;
        }
    }
    return (session->pending_len != 0);
}

/* This MUST return ESP_OK on successful execution. If any other
 * value is returned, everything related to this socket will be
 * cleaned up and the socket will be closed.
 */
esp_err_t httpd_sess_process(struct httpd_data *hd, struct sock_db *session)
{
    if ((!hd) || (!session)) {
        return ESP_FAIL;
    }

    LOGD(TAG, LOG_FMT("httpd_req_new"));
    if (httpd_req_new(hd, session) != ESP_OK) {
        return ESP_FAIL;
    }

    // Get the HTTP version and Connection header for connection persistence
    const char *http_version = hd->hd_req.version;
    const char *connection_value = ((struct httpd_req_aux *)hd->hd_req.aux)->connection_hdr;

    // Fall back to trying to get Connection header value if not stored during parsing
    if (!connection_value || connection_value[0] == '\0') {
        char conn_hdr[64] = {0};
        esp_err_t hdr_result = httpd_req_get_hdr_value_str(&hd->hd_req, "Connection",
                                                         conn_hdr, sizeof(conn_hdr));
        if (hdr_result == ESP_OK) {
            connection_value = conn_hdr;
        }
    }

    // Process HTTP/1.1 connection persistence headers
    esp_err_t conn_result = httpd_connection_process_headers(hd, session->fd,
                                                           http_version, connection_value);
    if (conn_result != ESP_OK) {
        LOGW(TAG, "Failed to process connection headers for fd=%d", session->fd);
        httpd_req_delete(hd);
        return ESP_FAIL;
    }

    // Increment request counter and update timestamp
    httpd_connection_increment_request_count(hd, session->fd);

    LOGD(TAG, LOG_FMT("httpd_req_delete"));
    if (httpd_req_delete(hd) != ESP_OK) {
        return ESP_FAIL;
    }

    LOGD(TAG, LOG_FMT("success"));
    session->lru_counter = ++hd->lru_counter;

    // Update connection timestamp
    httpd_connection_update_timestamp(hd, session->fd);

    // Check if connection should be closed after this response
    if (httpd_connection_should_close_after_response(hd, session->fd)) {
        LOGD(TAG, "Connection fd=%d marked for closure after response", session->fd);
        // For async requests, the existing logic will handle the close
        if (session->for_async_req) {
            session->close_after_async_complete = true;
        } else {
            // For synchronous requests, close immediately if not persistent
            if (!httpd_connection_should_persist(hd, session->fd)) {
                httpd_sess_delete(hd, session);
            }
        }
    } else if (!httpd_connection_should_persist(hd, session->fd)) {
        // Connection is not persistent (e.g., client sent "Connection: close"), close immediately
        LOGD(TAG, "Connection fd=%d is not persistent, closing after response", session->fd);
        if (session->for_async_req) {
            session->close_after_async_complete = true;
        } else {
            httpd_sess_delete(hd, session);
        }
    } else {
        // Legacy handling: close connection when client explicitly requests it
        bool legacy_close_request = connection_value &&
                                   strcasecmp(connection_value, "close") == 0;

        // Handle connection close based on async status
        if (session->for_async_req) {
            if (legacy_close_request) {
                session->close_after_async_complete = true;
            }
        } else {
            if (legacy_close_request) {
                httpd_sess_delete(hd, session);
            }
        }
    }

    return ESP_OK;
}

esp_err_t httpd_sess_update_lru_counter(httpd_handle_t handle, int sockfd)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct httpd_data *hd = (struct httpd_data *) handle;

    enum_context_t context = {
        .task = HTTPD_TASK_FIND_FD,
        .fd = sockfd
    };
    httpd_sess_enum(hd, enum_function, &context);
    if (context.session) {
        context.session->lru_counter = ++hd->lru_counter;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t httpd_sess_close_lru(struct httpd_data *hd)
{
    enum_context_t context = {
        .task = HTTPD_TASK_FIND_LOWEST_LRU,
        .lru_counter = UINT64_MAX,
        .fd = -1
    };
    httpd_sess_enum(hd, enum_function, &context);
    if (!context.session) {
        return ESP_OK;
    }
    LOGD(TAG, LOG_FMT("Closing session with fd %d"), context.session->fd);
    context.session->lru_socket = true;
    return httpd_sess_trigger_close_(hd, context.session);
}

esp_err_t httpd_sess_trigger_close_(httpd_handle_t handle, struct sock_db *session)
{
    if (!session) {
        return ESP_ERR_NOT_FOUND;
    }
    return httpd_queue_work(handle, httpd_sess_close, session);
}

esp_err_t httpd_sess_trigger_close(httpd_handle_t handle, int sockfd)
{
    struct sock_db *session = httpd_sess_get(handle, sockfd);
    if (!session) {
        return ESP_ERR_NOT_FOUND;
    }
    return httpd_sess_trigger_close_(handle, session);
}

void httpd_sess_close_all(struct httpd_data *hd)
{
    enum_context_t context = {
        .task = HTTPD_TASK_CLOSE,
        .hd = hd
    };
    httpd_sess_enum(hd, enum_function, &context);
}
