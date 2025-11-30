#ifndef _HTTPD_PORT_NETWORK_H_
#define _HTTPD_PORT_NETWORK_H_

#define CONFIG_IDF_TARGET_LINUX 1

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h> // For sockaddr_in6
    #include <mstcpip.h>  // Contains the SIO_KEEPALIVE_VALS command
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
#endif

#define CONFIG_LWIP_IPV4 1

#ifdef _WIN32
int inet_aton(const char *cp, struct in_addr *addr);
#endif

/**
 * @brief Set socket options
 *
 * @param sockfd The socket file descriptor
 * @param level The protocol level for the option
 * @param optname The option name
 * @param optval Pointer to the option value
 * @param optlen Size of the option value
 * @return 0 on success, -1 on failure
 */
static inline int httpd_sock_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
#ifdef _WIN32
    return setsockopt(sockfd, level, optname, (const char*)optval, optlen);
#else
    return setsockopt(sockfd, level, optname, optval, optlen);
#endif
}

#endif // _HTTPD_PORT_NETWORK_H_
