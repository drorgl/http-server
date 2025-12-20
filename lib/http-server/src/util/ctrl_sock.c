/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "log.h"

#include "../port/win/network.h"

#ifdef ESP_PLATFORM
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "sdkconfig.h"
#include "ctrl_sock.h"
#endif

#if CONFIG_IDF_TARGET_LINUX
#define IPV4_ENABLED      1
#define IPV6_ENABLED      1
#define LOOPBACK_ENABLED  1
#else   // CONFIG_IDF_TARGET_LINUX
#define IPV4_ENABLED      CONFIG_LWIP_IPV4
#define IPV6_ENABLED      CONFIG_LWIP_IPV6
#define LOOPBACK_ENABLED  CONFIG_LWIP_NETIF_LOOPBACK
#endif  // !CONFIG_IDF_TARGET_LINUX

static const char * TAG = "ctrl-sock";

/* Control socket, because in some network stacks select can't be woken up any
 * other way
 */
int cs_create_ctrl_sock(int port)
{
#if !LOOPBACK_ENABLED
    LOGE(TAG, "Please enable LWIP_NETIF_LOOPBACK for %s API", __func__);
    return -1;
#endif

    LOGD(TAG, "creating control socket on port %d", port);
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        LOGE(TAG, "error creating control socket (%d)", errno);
        return -1;
    }
    LOGD(TAG, "created socket fd=%d", fd);

    int ret;
    struct sockaddr_storage addr = {};
    socklen_t addr_len = 0;
#if IPV4_ENABLED
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(port);
    inet_aton("127.0.0.1", &addr4->sin_addr);
    addr_len = sizeof(struct sockaddr_in);
    LOGD(TAG, "binding to IPv4 address 127.0.0.1:%d", port);
#else
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(port);
    inet6_aton("::1", &addr6->sin6_addr);
    addr_len = sizeof(struct sockaddr_in6);
    LOGD(TAG, "binding to IPv6 address ::1:%d", port);
#endif
    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable)) < 0) {
        /* This will fail if CONFIG_LWIP_SO_REUSE is not enabled. But
         * it does not affect the normal working of the HTTP Server */
        LOGW(TAG, "error in setsockopt SO_REUSEADDR (%d)", errno);
    }

#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char*)&enable, sizeof(enable)) < 0) {
        /* This will fail if SO_REUSEPORT is not supported. But
         * it does not affect the normal working of the HTTP Server */
        LOGW(TAG, "error in setsockopt SO_REUSEPORT (%d)", errno);
    }
#endif

    ret = bind(fd, (struct sockaddr *)&addr, addr_len);
    if (ret < 0) {
        LOGE(TAG, "error binding control socket (%d)", errno);
        close(fd);
        return -1;
    }
    LOGD(TAG, "control socket created successfully fd=%d", fd);
    
    // Verify the socket is properly bound by getting its local address
    struct sockaddr_storage local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(fd, (struct sockaddr *)&local_addr, &local_addr_len) == 0) {
        if (local_addr.ss_family == AF_INET) {
            struct sockaddr_in *local_addr4 = (struct sockaddr_in *)&local_addr;
            LOGD(TAG, "socket fd=%d bound to local address 127.0.0.1:%d", fd, ntohs(local_addr4->sin_port));
        } else if (local_addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *local_addr6 = (struct sockaddr_in6 *)&local_addr;
            LOGD(TAG, "socket fd=%d bound to local address ::1:%d", fd, ntohs(local_addr6->sin6_port));
        }
    } else {
        LOGE(TAG, "error getting local address for socket fd=%d (%d)", fd, errno);
    }
    
    return fd;
}

void cs_free_ctrl_sock(int fd)
{
    LOGD(TAG, "freeing control socket fd=%d", fd);
    if (fd == -1){
        LOGE(TAG, "Attempted to Close an invalid socket %d", fd);
        return;
    }

#ifdef _WIN32
    if (closesocket(fd) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        LOGE(TAG, "Error Closing Socket %d: %d", fd, err);
    }
#else
    close(fd);
#endif
}

int cs_send_to_ctrl_sock(int send_fd, int port, void *data, unsigned int data_len)
{
    LOGD(TAG, "sending to control socket (%d) on port %d data_len %u", send_fd, port, data_len);
    int ret;
    struct sockaddr_storage to_addr = {};
    socklen_t addr_len = 0;
#if IPV4_ENABLED
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&to_addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(port);
    inet_aton("127.0.0.1", &addr4->sin_addr);
    addr_len = sizeof(struct sockaddr_in);
    LOGD(TAG, "sending to IPv4 address 127.0.0.1:%d", port);
#else
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&to_addr;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(port);
    inet6_aton("::1", &addr6->sin6_addr);
    addr_len = sizeof(struct sockaddr_in6);
    LOGD(TAG, "sending to IPv6 address ::1:%d", port);
#endif
    // Verify the sending socket is also bound to allow receiving
    struct sockaddr_storage send_local_addr;
    socklen_t send_local_addr_len = sizeof(send_local_addr);
    if (getsockname(send_fd, (struct sockaddr *)&send_local_addr, &send_local_addr_len) == 0) {
        if (send_local_addr.ss_family == AF_INET) {
            struct sockaddr_in *send_local_addr4 = (struct sockaddr_in *)&send_local_addr;
            LOGD(TAG, "send socket fd=%d bound to local address 0.0.0.0:%d", send_fd, ntohs(send_local_addr4->sin_port));
        }
    }
    
    ret = sendto(send_fd, data, data_len, 0, (struct sockaddr *)&to_addr, addr_len);
    if (ret < 0) {
        LOGE(TAG, "error sending to control socket (%d)", errno);
        return -1;
    }
    LOGD(TAG, "successfully sent %d bytes to control socket (%d)", ret, send_fd);
    return ret;
}

int cs_recv_from_ctrl_sock(int fd, void *data, unsigned int data_len)
{
    LOGD(TAG, "receiving from control socket fd=%d", fd);
    int ret;
    ret = recvfrom(fd, data, data_len, 0, NULL, NULL);

    if (ret < 0) {
        LOGE(TAG, "error receiving from control socket (%d)", errno);
        return -1;
    }
    LOGD(TAG, "successfully received %d bytes from control socket", ret);
    return ret;
}
