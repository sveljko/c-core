/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/sockets/pbpal_adns_sockets.h"

#include "pubnub_internal.h"

#include "core/pubnub_log.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#define CAST
#else
#include <ws2tcpip.h>
#define CAST (int*)
#endif

#include <stdint.h>


#if PUBNUB_LOG_LEVEL >= PUBNUB_LOG_LEVEL_TRACE
#define TRACE_SOCKADDR(str, addr, sockaddr_size)                               \
    do {                                                                       \
        char M_h_[50];                                                         \
        char M_s_[20];                                                         \
        getnameinfo(addr,                                                      \
                    sockaddr_size,                                             \
                    M_h_,                                                      \
                    sizeof M_h_,                                               \
                    M_s_,                                                      \
                    sizeof M_s_,                                               \
                    NI_NUMERICHOST | NI_NUMERICSERV);                          \
        PUBNUB_LOG_TRACE(str "%s-port:%s\n", M_h_, M_s_);                      \
    } while (0)
#else
#define TRACE_SOCKADDR(str, addr)
#endif


int send_dns_query(int skt,
                   struct sockaddr const* dest,
                   char const* host,
                   enum DNSqueryType query_type)
{
    uint8_t buf[4096];
    int     to_send;
    int     sent_to;
    size_t  sockaddr_size;

    switch (dest->sa_family) {
    case AF_INET: 
        sockaddr_size = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        sockaddr_size = sizeof(struct sockaddr_in6);
        break;
    default:
        PUBNUB_LOG_ERROR("send_dns_query(socket=%d): invalid address family "
                         "dest->sa_family =%uh\n",
                         skt,
                         dest->sa_family);
        return -1;
    }
    if (-1 == pbdns_prepare_dns_request(buf, sizeof buf, host, &to_send, query_type)) {
        PUBNUB_LOG_ERROR("Couldn't prepare dns request! : #prepared bytes=%d\n", to_send);
        return -1;
    }
    TRACE_SOCKADDR("Sending DNS query to: ", dest, sockaddr_size);
    sent_to       = sendto(skt, (char*)buf, to_send, 0, dest, sockaddr_size);
    if (sent_to <= 0) {
        return socket_would_block() ? +1 : -1;
    }
    else if (to_send != sent_to) {
        PUBNUB_LOG_ERROR("sendto() sent %d out of %d bytes!\n", sent_to, to_send);
        return -1;
    }
    return 0;
}


int read_dns_response(int skt, struct sockaddr* dest, struct sockaddr** resolved_addr)
{
    uint8_t                   buf[8192];
    int                       msg_size;
    unsigned                  sockaddr_size;
    union pubnub_ipvX_address temp_resolved_addr;
    enum DNSqueryType         addr_type;

    switch (dest->sa_family) {
    case AF_INET: 
        sockaddr_size = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        sockaddr_size = sizeof(struct sockaddr_in6);
        break;
    default:
        PUBNUB_LOG_ERROR("read_dns_response(socket=%d): invalid address family "
                         "dest->sa_family =%uh\n",
                         skt,
                         dest->sa_family);
        return -1;
    }
    msg_size = recvfrom(skt, (char*)buf, sizeof buf, 0, dest, CAST & sockaddr_size);
    if (msg_size <= 0) {
        return socket_would_block() ? +1 : -1;
    }

    if (pbdns_pick_resolved_address(buf, (size_t)msg_size, &temp_resolved_addr, &addr_type) != 0) {
        return -1;
    }
    switch (addr_type) {
    case dnsA:
        {
            /* This memory must be liberated(freed) in 'connecting' function */
            struct sockaddr_in* p_addr_st = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
            
            memcpy(&(p_addr_st->sin_addr.s_addr),
                   &(temp_resolved_addr.ipv4),
                   sizeof(struct pubnub_ipv4_address));
            *resolved_addr = (struct sockaddr*)p_addr_st;
            (*resolved_addr)->sa_family = AF_INET;
        }
        return 0;
    case dnsAAAA:
        {
            /* This memory must be liberated(freed) in 'connecting' function */
            struct sockaddr_in6* p_addr_st = (struct sockaddr_in6*)malloc(sizeof(struct sockaddr_in6));

            memcpy(p_addr_st->sin6_addr.s6_addr,
                   &(temp_resolved_addr.ipv6),
                   sizeof(struct pubnub_ipv6_address));
            *resolved_addr = (struct sockaddr*)p_addr_st;
            (*resolved_addr)->sa_family = AF_INET6;
        }
        return 0;
    default:
        PUBNUB_LOG_ERROR("read_dns_response(socket=%d): invalid address type "
                         "addr_type=%uh\n",
                         skt,
                         addr_type);
        return -1;
    }
}


//#if 0
#include <stdio.h>
#include <fcntl.h>
int main()
{
    struct sockaddr_in dest;
    struct sockaddr_in6 dest6;

    puts("===========================ADNS-AF_INET========================");

    int skt = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int flags = fcntl(skt, F_GETFL, 0);
    fcntl(skt, F_SETFL, flags | O_NONBLOCK);

    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr("208.67.222.222");

    if (-1 == send_dns_query(skt, (struct sockaddr*)&dest, "facebook.com", dnsANY)) {
        PUBNUB_LOG_ERROR("Error: Couldn't send datagram(Ipv4).\n");
        return -1;
    }

    fd_set read_set;
    int rslt;
    struct timeval timev = { 0, 300000 };

    FD_ZERO(&read_set);
    FD_SET(skt, &read_set);
    rslt = select(skt + 1, &read_set, NULL, NULL, &timev);
    if (-1 == rslt) {
        puts("select() Error!\n");
        return -1;
    }
    else if (rslt > 0) {
        struct sockaddr* resolved_addr;
        printf("skt=%d, rslt=%d, timev.tv_sec=%ld, timev.tv_usec=%ld\n", skt, rslt, timev.tv_sec, timev.tv_usec);
        read_dns_response(skt, (struct sockaddr*)&dest, &resolved_addr);
    }
    else {
        puts("no select() event(Ipv4).");
    }
    socket_close(skt);

    puts("=========================ADNS-AF_INET6========================");

    skt = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    flags = fcntl(skt, F_GETFL, 0);
    fcntl(skt, F_SETFL, flags | O_NONBLOCK);
    timev.tv_sec = 0;
    timev.tv_usec = 300000;

    dest6.sin6_family = AF_INET6;
    dest6.sin6_port = htons(53);
    inet_pton(AF_INET6, "2620:119:35::35", &dest6.sin6_addr);

    if (-1 == send_dns_query(skt, (struct sockaddr*)&dest6, "facebook.com", dnsANY)) {
        PUBNUB_LOG_ERROR("Error: Couldn't send datagram(Ipv6).\n");
        return -1;
    }

    FD_ZERO(&read_set);
    FD_SET(skt, &read_set);
    rslt = select(skt + 1, &read_set, NULL, NULL, &timev);
    if (-1 == rslt) {
        puts("select() Error!\n");
        return -1;
    }
    else if (rslt > 0) {
        struct sockaddr* resolved_addr;
        printf("skt=%d, rslt=%d, timev.tv_sec=%ld, timev.tv_usec=%ld\n", skt, rslt, timev.tv_sec, timev.tv_usec);
        read_dns_response(skt, (struct sockaddr*)&dest6, &resolved_addr);
    }
    else {
        puts("no select() event(Ipv6).");
    }
    socket_close(skt);

    return 0;
}
//#endif
