/* wii_net.h — POSIX socket shim for libogc; maps BSD socket calls to net_* equivalents */
#ifndef WII_NET_H
#define WII_NET_H

#undef INVALID_SOCKET
#undef SOCKET_ERROR
#ifndef WII_NO_NETWORK_H
#include <network.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/* Undef gccore/color.h COLOR_* so q_shared.h can define Q3 console colour chars */
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_WHITE

#ifdef __cplusplus
extern "C" {
#endif

static char wii_net_local_ip[16];

/* 1 if net_init() succeeded (IOS socket device open) */
static int s_net_ip_top_ready = 0;

static inline int Wii_Net_Init(void)
{
    char netmask[16] = {0};
    char gateway[16] = {0};
    wii_net_local_ip[0] = '\0';

    {
        s32 r;
        int tries = 0;
        do {
            r = net_init();
            if (r == -EAGAIN || r == -24) usleep(100000);
        } while ((r == -EAGAIN || r == -24) && ++tries < 50);

        if (r < 0)
            return (int)r;

        s_net_ip_top_ready = 1;
    }

    /* DHCP with a SMALL retry count. This runs synchronously in main() before
     * Com_Init/the renderer, so a large retry count means no-router / unplugged
     * cable blocks the entire boot (black screen forever) waiting for a lease
     * that never arrives. 3 retries bounds the wait to a few seconds; a working
     * network answers in well under that. The result is best-effort and non-fatal
     * (main() discards it) — online play simply works once a network is present. */
    int ifr = if_config(wii_net_local_ip, netmask, gateway, 1, 3);
    return ifr;
}

/* fcntl shim — libogc uses net_ioctl(FIONBIO) for non-blocking mode */
static inline int wii_net_fcntl(int s, int cmd, int arg)
{
    if (cmd == F_SETFL && (arg & O_NONBLOCK)) {
        u32 nb = 1;
        return net_ioctl(s, FIONBIO, &nb);
    }
    if (cmd == F_SETFL && !(arg & O_NONBLOCK)) {
        u32 nb = 0;
        return net_ioctl(s, FIONBIO, &nb);
    }
    return 0;
}

#undef  close
#define close(s)                    net_close(s)

#undef  fcntl
#define fcntl(s,cmd,arg)            wii_net_fcntl((s),(cmd),(arg))

/* socket(): IOS requires protocol=0 (infers from type); normalize errors to -1 */
static inline int wii_net_socket(int d, int t, int p)
{
    (void)p;
    int fd = net_socket(d, t, 0);
    if (fd < 0) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (t == SOCK_DGRAM) {
        int one = 1;
        net_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    }
    return fd;
}
#define socket(d,t,p)               wii_net_socket(d,t,p)

/* bind(): set sin_len (IOS BSD-style) and normalize error codes */
static inline int wii_net_bind(int s, struct sockaddr *a, int l)
{
    struct sockaddr_in tmp4;
    if (a && ((struct sockaddr_in *)a)->sin_family == AF_INET) {
        tmp4 = *(struct sockaddr_in *)a;
        tmp4.sin_len = (uint8_t)sizeof(struct sockaddr_in);
        a = (struct sockaddr *)&tmp4;
    }
    int ret = net_bind(s, a, l);
    if (ret < 0 && ret != -1) { errno = EADDRINUSE; return -1; }
    return ret;
}
#define bind(s,a,l)                 wii_net_bind(s,a,l)
#define connect(s,a,l)              net_connect((s),(a),(l))
#define listen(s,b)                 net_listen((s),(b))
#define accept(s,a,l)               net_accept((s),(a),(l))
#ifdef WII_NET_NEED_SEND
#define send(s,b,l,f)               net_send((s),(b),(l),(f))
#endif
#define recv(s,b,l,f)               net_recv((s),(b),(l),(f))

/* sendto: build 8-byte POSIX addr for IOS (no sin_len prefix, tolen=8) */
static inline int wii_net_sendto(int s, const void *b, int l, int f,
                                  const struct sockaddr *a, int al)
{
    uint8_t ios_addr[8];
    const struct sockaddr *send_a  = a;
    int                    send_al = al;
    if (a && a->sa_family == AF_INET) {
        const struct sockaddr_in *sin4 = (const struct sockaddr_in *)a;
        uint16_t fam = (uint16_t)AF_INET;
        memcpy(ios_addr + 0, &fam,            2);
        memcpy(ios_addr + 2, &sin4->sin_port, 2);
        memcpy(ios_addr + 4, &sin4->sin_addr, 4);
        send_a  = (const struct sockaddr *)ios_addr;
        send_al = 8;
    }
    int ret = net_sendto(s, (void *)b, l, f, (struct sockaddr *)send_a, send_al);
    return ret;
}
#define sendto(s,b,l,f,a,al) wii_net_sendto((s),(b),(l),(f),(a),(al))

/* recvfrom: normalize IOS error codes to -1 + errno=EAGAIN */
static inline int wii_net_recvfrom(int s, void *b, int l, int f,
                                    struct sockaddr *a, socklen_t *al)
{
    int ret = net_recvfrom(s, b, l, f, a, al);
    if (ret >= 0) return ret;
    errno = EAGAIN;
    return -1;
}
#define recvfrom(s,b,l,f,a,al)      wii_net_recvfrom((s),(b),(l),(f),(a),(al))
#define setsockopt(s,lv,o,v,l)      net_setsockopt((s),(lv),(o),(v),(l))
#define getsockopt(s,lv,o,v,l)      net_getsockopt((s),(lv),(o),(v),(l))

/* select: bypass net_select (blocks with zero timeout); always return 1 */
static inline int wii_net_select(int n, fd_set *r, fd_set *w, fd_set *e,
                                  struct timeval *t)
{
    (void)n; (void)r; (void)w; (void)e; (void)t;
    return 1;
}
#define select(n,r,w,e,t)           wii_net_select((n),(r),(w),(e),(t))
#define gethostbyname(n)            net_gethostbyname(n)

/* errno values newlib may be missing */
#ifndef EWOULDBLOCK
#  define EWOULDBLOCK  EAGAIN
#endif
#ifndef EINPROGRESS
#  define EINPROGRESS  36
#endif
#ifndef ECONNREFUSED
#  define ECONNREFUSED 111
#endif

#ifdef __cplusplus
}
#endif

#endif /* WII_NET_H */
