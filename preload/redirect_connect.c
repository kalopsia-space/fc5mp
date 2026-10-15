#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int (*real_connect)(int, const struct sockaddr *, socklen_t) = NULL;
static ssize_t (*real_sendto)(int, const void *, size_t, int,
                              const struct sockaddr *, socklen_t) = NULL;
static ssize_t (*real_recvfrom)(int, void *, size_t, int,
                                struct sockaddr *, socklen_t *) = NULL;
static ssize_t (*real_sendmsg)(int, const struct msghdr *, int) = NULL;
static ssize_t (*real_recvmsg)(int, struct msghdr *, int) = NULL;

static in_addr_t server_ip = 0;
static int inited = 0;
static int log_fd = -1;
static int udp_fd = -1; // track the PRUDP socket fd

static void log_msg(const char *msg) {
    if (log_fd >= 0) write(log_fd, msg, strlen(msg));
}

static void lazy_init(void) {
    if (inited) return;
    inited = 1;
    real_connect = dlsym(RTLD_NEXT, "connect");
    real_sendto = dlsym(RTLD_NEXT, "sendto");
    real_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    real_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    real_recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    const char *ip = getenv("FC5MP_SERVER_IP");
    server_ip = inet_addr(ip && ip[0] ? ip : "127.0.0.1");
    log_fd = open("/tmp/fc5mp-preload.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    log_msg("fc5mp preload shim loaded\n");
}

static int is_port_6570(const struct sockaddr *addr, socklen_t len) {
    if (!addr || len < sizeof(struct sockaddr_in)) return 0;
    if (addr->sa_family != AF_INET) return 0;
    return ntohs(((const struct sockaddr_in *)addr)->sin_port) == 6570;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    lazy_init();
    if (addr && addr->sa_family == AF_INET &&
        addrlen >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        if (ntohs(sin->sin_port) == 6570) {
            int type = 0;
            socklen_t optlen = sizeof(type);
            if (getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &optlen) == 0 &&
                type == SOCK_DGRAM) {
                struct sockaddr_in rw;
                memcpy(&rw, sin, sizeof(rw));
                rw.sin_addr.s_addr = server_ip;
                char buf[128];
                snprintf(buf, sizeof(buf), "connect fd=%d port=6570 DGRAM -> REDIRECT ret=", sockfd);
                log_msg(buf);
                int ret = real_connect(sockfd,
                                       (const struct sockaddr *)&rw, sizeof(rw));
                snprintf(buf, sizeof(buf), "%d\n", ret);
                log_msg(buf);
                return ret;
            }
        }
    }
    return real_connect(sockfd, addr, addrlen);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    lazy_init();
    if (is_port_6570(dest_addr, addrlen)) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)dest_addr;
        uint32_t ip_h = ntohl(sin->sin_addr.s_addr);
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "sendto fd=%d len=%zu port=6570 ip=%u.%u.%u.%u\n",
                 sockfd, len,
                 (ip_h>>24)&0xFF,(ip_h>>16)&0xFF,(ip_h>>8)&0xFF,ip_h&0xFF);
        log_msg(msg);
        udp_fd = sockfd;
    }
    return real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    lazy_init();
    ssize_t ret = real_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    if (sockfd == udp_fd && ret >= 0) {
        char msg[256];
        if (src_addr && addrlen && *addrlen >= sizeof(struct sockaddr_in) &&
            src_addr->sa_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)src_addr;
            uint32_t ip_h = ntohl(sin->sin_addr.s_addr);
            snprintf(msg, sizeof(msg),
                     "recvfrom fd=%d ret=%zd port=%u ip=%u.%u.%u.%u\n",
                     sockfd, ret, ntohs(sin->sin_port),
                     (ip_h>>24)&0xFF,(ip_h>>16)&0xFF,(ip_h>>8)&0xFF,ip_h&0xFF);
        } else {
            snprintf(msg, sizeof(msg), "recvfrom fd=%d ret=%zd\n", sockfd, ret);
        }
        log_msg(msg);
    }
    return ret;
}

static void log_hex(const void *data, size_t len) {
    const unsigned char *p = data;
    char hex[4];
    log_msg(" hex=");
    for (size_t i = 0; i < len && i < 64; i++) {
        snprintf(hex, sizeof(hex), "%02x", p[i]);
        log_msg(hex);
    }
    log_msg("\n");
}

static void log_iov_hex(const struct iovec *iov, size_t iovlen, size_t total) {
    log_msg(" hex=");
    char hex[4];
    size_t logged = 0;
    for (size_t i = 0; i < iovlen && logged < 64; i++) {
        const unsigned char *p = iov[i].iov_base;
        for (size_t j = 0; j < iov[i].iov_len && logged < 64; j++, logged++) {
            snprintf(hex, sizeof(hex), "%02x", p[j]);
            log_msg(hex);
        }
    }
    log_msg("\n");
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    lazy_init();
    if (msg && msg->msg_name && is_port_6570(msg->msg_name, msg->msg_namelen)) {
        size_t total = 0;
        for (size_t i = 0; i < (size_t)msg->msg_iovlen; i++)
            total += msg->msg_iov[i].iov_len;
        char buf[128];
        snprintf(buf, sizeof(buf), "sendmsg fd=%d len=%zu port=6570", sockfd, total);
        log_msg(buf);
        log_iov_hex(msg->msg_iov, msg->msg_iovlen, total);
        udp_fd = sockfd;
    }
    return real_sendmsg(sockfd, msg, flags);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    lazy_init();
    ssize_t ret = real_recvmsg(sockfd, msg, flags);
    if (sockfd == udp_fd && ret >= 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "recvmsg fd=%d ret=%zd", sockfd, ret);
        log_msg(buf);
        if (ret > 0 && msg->msg_iovlen > 0 && msg->msg_iov[0].iov_base) {
            size_t dump = (size_t)ret < msg->msg_iov[0].iov_len ? (size_t)ret : msg->msg_iov[0].iov_len;
            log_hex(msg->msg_iov[0].iov_base, dump);
        } else {
            log_msg("\n");
        }
    }
    return ret;
}
