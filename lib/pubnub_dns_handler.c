/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/pubnub_dns_handler.h"

#include "pubnub_internal.h"

#include "core/pubnub_assert.h"
#include "core/pubnub_log.h"

#include <string.h>


/** Size of DNS header, in octets */
#define HEADER_SIZE 12

/** Offset of the ID field in the DNS header */
#define HEADER_ID_OFFSET 0

/** Offset of the options field in the DNS header */
#define HEADER_OPTIONS_OFFSET 2

/** Offset of the query count field in the DNS header */
#define HEADER_QUERY_COUNT_OFFSET 4

/** Offset of the answer count field in the DNS header */
#define HEADER_ANSWER_COUNT_OFFSET 6

/** Offset of the authoritive servers count field in the DNS header */
#define HEADER_AUTHOR_COUNT_OFFSET 8

/** Offset of the additional records count field in the DNS header */
#define HEADER_ADDITI_COUNT_OFFSET 10

/** Bits for the options field of the DNS header - two octets
    in total.
 */
enum DNSoptionsMask {
    /* Response type;  0: no error, 1: format error, 2: server fail,
       3: name eror, 4: not implemented, 5: refused
    */
    dnsoptRCODEmask = 0x000F,
    /** Checking disabled */
    dnsoptCDmask = 0x0010,
    /** Authentication data */
    dnsoptADmask = 0x0020,
    /** Recursion available */
    dnsoptRAmask = 0x0080,
    /** Recursion desired */
    dnsoptRDmask = 0x0100,
    /** Truncation (1 - message was truncated, 0 - was not) */
    dnsoptTCmask = 0x0200,
    /** Authorative answer */
    dnsoptAAmask = 0x0400,
    /** 0: query, 1: inverse query, 2: status */
    dnsoptOPCODEmask = 0x7800,
    /** 0: query, 1: response */
    dnsoptQRmask = 0x8000,
};


/** Size of non-name data in the QUESTION field of a DNS mesage,
    in octets */
#define QUESTION_DATA_SIZE 4

/** Size of non-name data in the RESOURCE DATA field of a DNS mesage,
    in octets */
#define RESOURCE_DATA_SIZE 10

/** Offset of the type sub-field of the RESOURCE DATA */
#define RESOURCE_DATA_TYPE_OFFSET 0

/** Offset of the data length sub-field of the RESOURCE DATA */
#define RESOURCE_DATA_DATA_LEN_OFFSET 8

/** Question/query types */
enum DNSqueryType {
    /** Address - IPv4 */
    dnsA = 1,
    /** Name server */
    dnsNS = 2,
    /** Canonical name */
    dnsCNAME = 5,
    /** Start of authority */
    dnsSOA = 6,
    /** Pointer (to another location in the name space ) */
    dnsPTR = 12,
    /** Mail exchange (responsible for handling e-mail sent to the
     * domain */
    dnsMX = 15,
    /** IPv6 address - 128 bit */
    dnsAAAA = 28,
    /** Service locator */
    dnsSRV = 33,
    /** All cached records */
    dnsANY = 255
};

/** Question/query class */
enum DNSqclass { dnsqclassInternet = 1 };


/* Maximum number of characters until the next dot('.'), or finishing NULL('\0') in array of bytes */
#define MAX_ALPHABET_STRETCH_LENGTH 63

/* Maximum passes through the loop allowed while decoding label, considering that it may
   contain five to six dots. Defined in order to detect erroneous offset pointers infinite loops. 
*/
#define MAXIMUM_LOOP_PASSES 10

/** Do the DNS QNAME (host) encoding. This strange kind of "run-time
    length encoding" will convert `"www.google.com"` to
    `"\3www\6google\3com"`.
    @param dns  Pointer to the buffer where encoded host label will be placed
    @param n    Maximum buffer length provided
    @param host Label to encode

    @return 'Encoded-dns-label-length' on success, -1 on failure
 */
static int dns_qname_encode(uint8_t* dns, size_t n, uint8_t const* host)
{
    uint8_t*             dest = dns + 1;
    uint8_t*             lpos;
    uint8_t const* const end = dns + n;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(host != NULL);
    PUBNUB_ASSERT_OPT(dns != NULL);

    lpos  = dns;
    *lpos = '\0';
    while (dest < end) {
        char hc = *host++;
        if ((hc != '.') && (hc != '\0')) {
            *dest++ = hc;
        }
        else {
            size_t d = dest - lpos;
            *dest++  = '\0';
            if (d > MAX_ALPHABET_STRETCH_LENGTH) {
                PUBNUB_LOG_ERROR("Error: in DNS label/name encoding.\n"
                                 "host    ='%s',\n"
                                 "encoded ='%s',\n"
                                 "Label to long: d=%u > MAX_ALPHABET_STRETCH_LENGTH=%d\n",
                                 host,
                                 dns,
                                 (unsigned)d,
                                 MAX_ALPHABET_STRETCH_LENGTH);
                return -1;
            }

            *lpos = (uint8_t)(d - 1);
            lpos += d;

            if ('\0' == hc) {
                break;
            }
        }
    }
    if ('\0' != *(dest - 1)) {
        PUBNUB_LOG_ERROR("Error: in DNS label/name encoding - "
                         "Buffer for encoded label too small: host='%s', n=%u, encoded='%s'\n",
                         host,
                         (unsigned)n,
                         dns);
        return -1;
    }

    return dest - dns;
}


int pubnub_prepare_dns_request(uint8_t* buf, size_t buf_size, unsigned char* host, int *to_send)
{
    int qname_encoded_length;

    *to_send = 0;	
    PUBNUB_ASSERT_OPT(buf_size > HEADER_SIZE);
    buf[HEADER_ID_OFFSET]              = 0;
    buf[HEADER_ID_OFFSET + 1]          = 33; /* in lack of a better ID */
    buf[HEADER_OPTIONS_OFFSET]         = dnsoptRDmask >> 8;
    buf[HEADER_OPTIONS_OFFSET + 1]     = 0;
    buf[HEADER_QUERY_COUNT_OFFSET]     = 0;
    buf[HEADER_QUERY_COUNT_OFFSET + 1] = 1;
    memset(buf + HEADER_ANSWER_COUNT_OFFSET,
           0,
           HEADER_SIZE - HEADER_ANSWER_COUNT_OFFSET);
    *to_send += HEADER_SIZE;

    qname_encoded_length = dns_qname_encode(buf + HEADER_SIZE, buf_size - HEADER_SIZE, host);
    if (qname_encoded_length <= 0) {
        return -1;
    }
    *to_send += qname_encoded_length;

    PUBNUB_ASSERT_OPT(buf_size - *to_send > QUESTION_DATA_SIZE);
    buf[*to_send]     = 0;
    buf[*to_send + 1] = dnsA;
    buf[*to_send + 2] = 0;
    buf[*to_send + 3] = dnsqclassInternet;
    *to_send += QUESTION_DATA_SIZE;

    return 0;
}


#define DNS_LABEL_DECODIG_FAILS(...) do {                                                  \
                                         PUBNUB_LOG(PUBNUB_LOG_LEVEL_ERROR,                \ 
                                                    "Error: in DNS label/name decoding - " \
                                                    __VA_ARGS__);                          \
                                         *dest = '\0';                                     \
                                         return -1;                                        \
                                     } while(0)  

/* Do the DNS label decoding. Apart from the RLE decoding of
   `3www6google3com0` -> `www.google.com`, it also has a
   "(de)compression" scheme in which a label can be shared with
   another in the same buffer.
   @param decoded         Pointer to memory section for decoded label
   @param n               Maximum length of the section in bytes
   @param src             address of the container with the offset(position within shared buffer
                          from where reading starts)
   @param buffer          Beginning of the buffer
   @param buffer_size     Complete buffer size
   @param o_bytes_to_skip If function succeeds, points to the offset(position within buffer)
                          available for 'next' buffer access
   @return 0 on success, -1 otherwise
*/
static int dns_label_decode(uint8_t*       decoded,
                            size_t         n,
                            uint8_t const* src,
                            uint8_t const* buffer,
                            size_t         buffer_size,
                            size_t*        o_bytes_to_skip)
{
    uint8_t*             dest   = decoded;
    uint8_t const* const end    = decoded + n;
    uint8_t const*       reader = src;
    uint8_t              pass   = 0;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(src != NULL);
    PUBNUB_ASSERT_OPT(buffer != NULL);
    PUBNUB_ASSERT_OPT(decoded != NULL);
    PUBNUB_ASSERT_OPT(o_bytes_to_skip != NULL);

    *o_bytes_to_skip = 0;
    while (dest < end) {
        uint8_t b = *reader;
        if (MAXIMUM_LOOP_PASSES < ++pass) {
            DNS_LABEL_DECODIG_FAILS("Too many passes(%d) through the loop.\n", pass);
        }
        if (0xC0 == (b & 0xC0)) {
            uint16_t offset = (b & 0x3F) * 256 + reader[1];
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 2;
            }
            if (offset >= buffer_size) {
                DNS_LABEL_DECODIG_FAILS("offset=%d >= buffer_size=%d\n", offset, (int)buffer_size);
            }
            reader = buffer + offset;
        }
        else if (0 == b) {
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 1;
            }
            return 0;
        }
        else if (0 == (b & 0xC0)) {
            if (dest != decoded) {
                *dest++ = '.';
            }
            if (dest + b >= end) {
                DNS_LABEL_DECODIG_FAILS("dest=%p + b=%d >= end=%p - "
                                        "Destination for decoding label/name too small, n=%u\n",
                                        dest,
                                        b,
                                        end,
                                        (unsigned)n);
            }
            if ((unsigned)(reader + b - buffer + 1) > buffer_size) {
                DNS_LABEL_DECODIG_FAILS("About to read outside the buffer:\n"
                                         "reader=%p + b=%d - buffer=%p + 1 >= buffer_size=%u\n",
                                         reader,
                                         b,
                                         buffer,
                                         (unsigned)buffer_size);
            }
            memcpy(dest, reader + 1, b);
            dest[b] = '\0';
            dest += b;
            reader += b + 1;
        }
        else {
            DNS_LABEL_DECODIG_FAILS("Bad offset format: b & 0xC0=%d, &b=%p\n", b & 0xC0, &b);
        }
    }

    /* If by any chance function reach this point n should be 0, but you just never know */
    PUBNUB_LOG_ERROR("Error: in DNS label/name decoding - "
                     "Destination for decoding label/name too small, n=%u\n", (unsigned)n);
    return -1;
}


int pubnub_pick_resolved_address(uint8_t* buf, int msg_size, struct sockaddr_in* resolved_addr)
{
    uint8_t* reader;
    size_t   q_count;
    size_t   ans_count;
    size_t   i;

    PUBNUB_ASSERT_OPT(buf != NULL);

    reader = buf + HEADER_SIZE;

    q_count = buf[HEADER_QUERY_COUNT_OFFSET] * 256
              + buf[HEADER_QUERY_COUNT_OFFSET + 1];
    ans_count = buf[HEADER_ANSWER_COUNT_OFFSET] * 256
                + buf[HEADER_ANSWER_COUNT_OFFSET + 1];
    PUBNUB_LOG_TRACE(
        "DNS response has: %hu Questions, %hu Answers.\n", q_count, ans_count);
    if (q_count != 1) {
        PUBNUB_LOG_INFO("Strange DNS response, we sent one question, but DNS "
                        "response doesn't have one question.\n");
    }
    for (i = 0; i < q_count; ++i) {
        uint8_t name[256];
        size_t  to_skip;

        if (0 != dns_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip)) {
            reader += to_skip + QUESTION_DATA_SIZE;
            continue;
        }
        PUBNUB_LOG_TRACE(
            "DNS response, %d. question name: %s, to_skip=%d\n", i+1, name, (int)to_skip);

        /* Could check for QUESTION data format (QType and QClass), but
           even if it's wrong, we don't know what to do with it, so,
           there's no use */
        reader += to_skip + QUESTION_DATA_SIZE;
    }
    for (i = 0; i < ans_count; ++i) {
        uint8_t  name[256];
        size_t   to_skip;
        size_t   r_data_len;
        unsigned r_data_len;

        if (0 != dns_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip)) {
            /* Even if label decoding fails(having offsets messed up, maybe), hopefully 'to_skip'
               will be set properly and we keep chasing good usable answer
            */
            r_data_len = reader[to_skip + RESOURCE_DATA_DATA_LEN_OFFSET] * 256
                         + reader[to_skip + RESOURCE_DATA_DATA_LEN_OFFSET + 1];
            reader += to_skip + RESOURCE_DATA_SIZE + r_data_len;
            continue;
        }
        r_data_len = reader[to_skip + RESOURCE_DATA_DATA_LEN_OFFSET] * 256
                     + reader[to_skip + RESOURCE_DATA_DATA_LEN_OFFSET + 1];
        r_data_type = reader[to_skip + RESOURCE_DATA_TYPE_OFFSET] * 256
                      + reader[to_skip + RESOURCE_DATA_TYPE_OFFSET + 1];
        reader += to_skip + RESOURCE_DATA_SIZE;

        PUBNUB_LOG_TRACE(
            "DNS %d. answer: %s, to_skip:%zu, type=%hu, data_len=%zu\n",
            i+1,
            name,
            to_skip,
            r_data_type,
            r_data_len);

        if (r_data_type == dnsA) {
            if (r_data_len != 4) {
                PUBNUB_LOG_WARNING("unexpected answer R_DATA length %zu\n",
                                   r_data_len);
                reader += r_data_len;
                continue;
            }
            PUBNUB_LOG_TRACE("Got IPv4: %u.%u.%u.%u\n",
                             reader[0],
                             reader[1],
                             reader[2],
                             reader[3]);
            resolved_addr->sin_family = AF_INET;
            memcpy(&resolved_addr->sin_addr, reader, 4);
            reader += r_data_len;
            return 0;
        }
        else {
            /* Don't care about other resource types, for now */
            reader += r_data_len;
        }
    }

    /* Don't care about Authoritative Servers or Additional records, for now */

    return -1;
}
