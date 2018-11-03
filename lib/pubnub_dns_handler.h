/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_DNS_HANDLER
#define      INC_PUBNUB_DNS_HANDLER

#include <stdint.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

/** Prepares DNS query request for @p host(name) in @p buf whose maximum available length is
    @p buf_size.
    If function succeedes, @p to_send 'carries' the length of prepared message.
    If function reports failure, @p to_send 'keeps' the length of successfully prepared segment
    before error occurred.
    
    @return 0 success, -1 failure
 */
int pubnub_prepare_dns_request(uint8_t* buf, size_t buf_size, unsigned char* host, int *to_send);

/** Picks valid resolved domain name address from the response from DNS server.
    @p buf points to the beginning of that response and @p msg_size is its length in octets.
    Upon success resolved address is placed in the corresponing structure pointed by @p resolved_addr

    @return 0 success, -1 failure
 */
int pubnub_pick_resolved_address(uint8_t* buf, int msg_size, struct sockaddr_in* resolved_addr);

#endif /* defined INC_PUBNUB_DNS_HANDLER */
