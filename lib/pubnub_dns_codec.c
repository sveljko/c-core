/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/pubnub_dns_codec.h"

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
#define RESOURCE_DATA_TYPE_OFFSET -10

/** Offset of the data length sub-field of the RESOURCE DATA */
#define RESOURCE_DATA_DATA_LEN_OFFSET -2

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

/* Maximum passes through the loop allowed while decoding label, considering that it may contain
   up to five or six dots(In case host name could have more dots, more passes should be alowed).
   Defined in order to detect erroneous offset pointers infinite loops.
   (Represents maximum  #encoded_lengths + #offsets detected while decoding)
*/
#define MAXIMUM_LOOP_PASSES 12

/** Do the DNS QNAME (host) encoding. This strange kind of "run-time
    length encoding" will convert `"www.google.com"` to
    `"\3www\6google\3com"`.
    @param dns  Pointer to the buffer where encoded host label will be placed
    @param n    Maximum buffer length provided
    @param host Label to encode

    @return 'Encoded-dns-label-length' on success, -1 on error
 */
static int dns_qname_encode(uint8_t* dns, size_t n, char const* host)
{
    uint8_t*             dest = dns + 1;
    uint8_t*             lpos;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(host != NULL);
    PUBNUB_ASSERT_OPT(dns != NULL);

    lpos  = dns;
    *lpos = '\0';
    for(;;) {
        char hc = *host++;
        if ((hc != '.') && (hc != '\0')) {
            *dest++ = hc;
        }
        else {
            size_t d = dest - lpos;
            *dest++  = '\0';
            if ((1 == d) || (MAX_ALPHABET_STRETCH_LENGTH < d - 1)) {
                PUBNUB_LOG_ERROR("Error: in DNS label/name encoding.\n"
                                 "host    ='%s',\n"
                                 "encoded ='%s',\n",
                                 host - (dest - 1 - dns),
                                 dns);
                if (d > 1) {
                    PUBNUB_LOG_ERROR("Label too long: stretch_length=%zu > MAX_ALPHABET_STRETCH_LENGTH=%d\n",
                                     d - 1,
                                     MAX_ALPHABET_STRETCH_LENGTH);
                }
                else {
                    PUBNUB_LOG_ERROR("Label stretch has no length.\n");
                }
                return -1;
            }

            *lpos = (uint8_t)(d - 1);
            lpos += d;

            if ('\0' == hc) {
                break;
            }
        }
    }

    return dest - dns;
}


int pubnub_prepare_dns_request(uint8_t* buf, size_t buf_size, char const* host, int *to_send)
{
    int qname_encoded_length;
    int len = 0;

    PUBNUB_ASSERT_OPT(buf != NULL);
    PUBNUB_ASSERT_OPT(host != NULL);
    PUBNUB_ASSERT_OPT(to_send != NULL);

    /* First encoded_length + label_end('\0') make 2 extra bytes */
    if (HEADER_SIZE + strlen((char*)host) + 2 + QUESTION_DATA_SIZE > buf_size) {
        *to_send = 0;
        PUBNUB_LOG_ERROR(
            "Error: While preparing DNS request - Buffer too small! buf_size=%zu, required_size=%zu\n"
            "host_name=%s\n",
            buf_size,
            HEADER_SIZE + strlen((char*)host) + 2 + QUESTION_DATA_SIZE,
            (char*)host);
        return -1;
    }
    buf[HEADER_ID_OFFSET]              = 0;
    buf[HEADER_ID_OFFSET + 1]          = 33; /* in lack of a better ID */
    buf[HEADER_OPTIONS_OFFSET]         = dnsoptRDmask >> 8;
    buf[HEADER_OPTIONS_OFFSET + 1]     = 0;
    buf[HEADER_QUERY_COUNT_OFFSET]     = 0;
    buf[HEADER_QUERY_COUNT_OFFSET + 1] = 1;
    memset(buf + HEADER_ANSWER_COUNT_OFFSET,
           0,
           HEADER_SIZE - HEADER_ANSWER_COUNT_OFFSET);
    len += HEADER_SIZE;

    qname_encoded_length = dns_qname_encode(buf + HEADER_SIZE, buf_size - HEADER_SIZE, host);
    if (qname_encoded_length <= 0) {
        *to_send = len;
        return -1;
    }
    len += qname_encoded_length;

    buf[len]     = 0;
    buf[len + 1] = dnsA;
    buf[len + 2] = 0;
    buf[len + 3] = dnsqclassInternet;
    *to_send = len + QUESTION_DATA_SIZE;

    return 0;
}

#define DNS_LABEL_DECODING_ERROR(...) do {                                                  \
                                         PUBNUB_LOG(PUBNUB_LOG_LEVEL_ERROR,                \
                                                    "Error: in DNS label/name decoding - " \
                                                    __VA_ARGS__);                          \
                                         *dest = '\0';                                     \
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
   @return 0 on success, -1 on error
*/
static int dns_label_decode(uint8_t*       decoded,
                            size_t         n,
                            uint8_t const* src,
                            uint8_t const* buffer,
                            size_t         buffer_size,
                            size_t*        o_bytes_to_skip)
{
    uint8_t*             dest        = decoded;
    uint8_t const* const end         = decoded + n;
    uint8_t const*       reader      = src;
    uint8_t              pass        = 0;
    bool                 forced_skip = false;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(src != NULL);
    PUBNUB_ASSERT_OPT(buffer != NULL);
    PUBNUB_ASSERT_OPT(src < buffer + buffer_size);
    PUBNUB_ASSERT_OPT(decoded != NULL);
    PUBNUB_ASSERT_OPT(o_bytes_to_skip != NULL);

    *o_bytes_to_skip = 0;
    for(;;) {
        uint8_t b = *reader;
        if (++pass > MAXIMUM_LOOP_PASSES) {
            DNS_LABEL_DECODING_ERROR("Too many passes(%hhu) through the loop.\n", pass);
            return -1;
        }
        if (0xC0 == (b & 0xC0)) {
            uint16_t offset;
            if ((reader + 1) >= (buffer + buffer_size)) {
                DNS_LABEL_DECODING_ERROR("About to read outside the buffer while reading offset:\n"
                                         "reader=%p + 1 >= buffer=%p + buffer_size=%zu\n",
                                         reader,
                                         buffer,
                                         buffer_size);
                return -1;
            }
            offset = (b & 0x3F) * 256 + reader[1];
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 2;
            }
            if (offset < HEADER_SIZE) {
                DNS_LABEL_DECODING_ERROR("Offset within header:offset=%hu, HEADER_SIZE=%d\n",
                                        offset,
                                        HEADER_SIZE);
                return -1;
            }
            if (offset >= buffer_size) {
                DNS_LABEL_DECODING_ERROR("offset=%hu >= buffer_size=%zu\n", offset, buffer_size);
                return -1;
            }
            reader = buffer + offset;
        }
        else if (0 == b) {
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 1;
            }
            if (*o_bytes_to_skip < 2) {
                DNS_LABEL_DECODING_ERROR(
                    "Something's wrong with message mapping or format! bytes_to_skip=%zu\n"
                    "Possibly misplaced offset, or labels to long.\n",
                    *o_bytes_to_skip);
                *o_bytes_to_skip = 0;

                return -1;
            }

            return forced_skip ? -1 : 0;
        }
        else if (0 == (b & 0xC0)) {
            if (!forced_skip) {
                if (dest != decoded) {
                    *dest++ = '.';
                }
                if (dest + b >= end) {
                    DNS_LABEL_DECODING_ERROR("dest=%p + b=%d >= end=%p - "
                                             "Destination for decoding label/name too small, n=%zu\n",
                                             dest,
                                             b,
                                             end,
                                             n);
                    forced_skip = true;
                }
            }
            /* (buffer + buffer_size) points to the first octet outside the buffer,
                while (reader + b + 1) has to be inside of it
             */
            if ((reader + b + 1) >= (buffer + buffer_size)) {
                DNS_LABEL_DECODING_ERROR(
                    "About to read outside the buffer while reading encoded label:\n"
                    "reader=%p + b=%d + 1 >= buffer=%p + buffer_size=%zu\n",
                    reader,
                    b,
                    buffer,
                    buffer_size);
                return -1;
            }
            if (!forced_skip) {
                memcpy(dest, reader + 1, b);
                dest[b] = '\0';
                dest += b;
            }
            reader += b + 1;
        }
        else {
            DNS_LABEL_DECODING_ERROR("Bad offset format: b & 0xC0=%d, &b=%p\n", b & 0xC0, &b);
            return -1;
        }
    }

    /* The only way to reach this code, at the time of this writing, is if n == 0, which we check
       with an 'assert', but it is left here 'just in case'*/
    PUBNUB_LOG_ERROR("Error: in DNS label/name decoding - "
                     "Destination for decoding label/name too small, n=%zu\n", n);
    return -1;
}

#define DNS_RESPONSE_ERR_OR_INCOMPLETE(...)                             \
   do {                                                                 \
        PUBNUB_LOG(PUBNUB_LOG_LEVEL_ERROR,                              \
                   "Error: DNS response erroneous, or incomplete:\n"    \
                   __VA_ARGS__);                                        \
   } while(0)  

int pubnub_pick_resolved_address(uint8_t* buf,
                                 int msg_size,
                                 struct pubnub_ipv4_address* resolved_addr)
{
    uint8_t* reader;
    size_t   q_count;
    size_t   ans_count;
    size_t   i;
    uint8_t* end;
    uint16_t options;

    PUBNUB_ASSERT_OPT(buf != NULL);
    PUBNUB_ASSERT_OPT(resolved_addr != NULL);

    if (HEADER_SIZE > msg_size) {
        PUBNUB_LOG_ERROR(
            "Error: DNS response is shorter than its header - msg_size=%d, HEADER_SIZE=%d\n",
            msg_size,
            HEADER_SIZE); 
        return -1;
    }
    options = ((uint16_t)buf[HEADER_OPTIONS_OFFSET] << 8) | (uint16_t)buf[HEADER_OPTIONS_OFFSET + 1];
    if (!(options & dnsoptQRmask)) {
        PUBNUB_LOG_ERROR("Error: DNS response doesn't have QR flag set!\n"); 
        return -1;
    }
    if (options & dnsoptRCODEmask) {
        PUBNUB_LOG_ERROR("Error: DNS response reports an error - RCODE = %d!\n",
                         buf[HEADER_OPTIONS_OFFSET + 1] & dnsoptRCODEmask); 
        return -1;
    }

    reader = buf + HEADER_SIZE;
    end    = buf + msg_size;

    q_count = buf[HEADER_QUERY_COUNT_OFFSET] * 256
              + buf[HEADER_QUERY_COUNT_OFFSET + 1];
    ans_count = buf[HEADER_ANSWER_COUNT_OFFSET] * 256
                + buf[HEADER_ANSWER_COUNT_OFFSET + 1];
    PUBNUB_LOG_TRACE(
        "DNS response has: %zu Questions, %zu Answers.\n", q_count, ans_count);
    if (q_count != 1) {
        PUBNUB_LOG_INFO("Strange DNS response, we sent one question, but DNS "
                        "response doesn't have one question.\n");
    }
    for (i = 0; i < q_count; ++i) {
        uint8_t name[256];
        size_t  to_skip;

        if (reader + QUESTION_DATA_SIZE > end) {
            DNS_RESPONSE_ERR_OR_INCOMPLETE(
                "reader=%p + QUESTION_DATA_SIZE=%d > buf=%p + msg_size=%d\n",
                reader,
                QUESTION_DATA_SIZE,
                buf,
                msg_size);
            return -1;
        }
        if ((dns_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip) != 0) &&
            (0 == to_skip)) {
            return -1;
        }
        PUBNUB_LOG_TRACE("DNS response, %zu. question name: %s, to_skip=%zu\n",
                         i+1,
                         name,
                         to_skip);

        /* Could check for QUESTION data format (QType and QClass), but
           even if it's wrong, we don't know what to do with it, so,
           there's no use */
        reader += to_skip + QUESTION_DATA_SIZE;
    }
    for (i = 0; i < ans_count; ++i) {
        uint8_t  name[256];
        size_t   to_skip;
        size_t   r_data_len;
        unsigned r_data_type;
        
        if (reader + RESOURCE_DATA_SIZE > end) {
            DNS_RESPONSE_ERR_OR_INCOMPLETE(
                "reader=%p + RESOURCE_DATA_SIZE=%d > buf=%p + msg_size=%d\n",
                reader,
                RESOURCE_DATA_SIZE,
                buf,
                msg_size);
            return -1;
        }
        /* Even if label decoding fails(having offsets messed up, maybe, or buffer too small for
           decoded label), 'to_skip' may be set(> 0) and we keep looking usable answer
         */
        if ((dns_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip) != 0) &&
            (0 == to_skip)) {
            return -1;
        }
        reader += to_skip + RESOURCE_DATA_SIZE;
        if (reader > end) {
            DNS_RESPONSE_ERR_OR_INCOMPLETE(
                "reader=%p > buf=%p + msg_size=%d :\n"
                "to_skip=%zu, RESOURCE_DATA_SIZE=%d\n",
                reader,
                buf,
                msg_size,
                to_skip,
                RESOURCE_DATA_SIZE);
            return -1;
        }
        /* Resource record data offsets are negative */
        r_data_len = reader[RESOURCE_DATA_DATA_LEN_OFFSET] * 256
                     + reader[RESOURCE_DATA_DATA_LEN_OFFSET + 1];
        if ((reader + r_data_len) > end) {
            DNS_RESPONSE_ERR_OR_INCOMPLETE(
                "reader=%p + r_data_len=%zu > buf=%p + msg_size=%d\n",
                reader,
                r_data_len,
                buf,
                msg_size);
            return -1;
        }
        r_data_type = reader[RESOURCE_DATA_TYPE_OFFSET] * 256
                      + reader[RESOURCE_DATA_TYPE_OFFSET + 1];
        PUBNUB_LOG_TRACE("DNS %zu. answer: %s, to_skip:%zu, type=%u, data_len=%zu\n",
                         i+1,
                         name,
                         to_skip,
                         r_data_type,
                         r_data_len);

        if (r_data_type == dnsA) {
            if (r_data_len != 4) {
                PUBNUB_LOG_ERROR("Error: Unexpected answer R_DATA length %zu\n",
                                 r_data_len);
                reader += r_data_len;
                continue;
            }
            PUBNUB_LOG_TRACE("Got IPv4: %u.%u.%u.%u\n",
                             reader[0],
                             reader[1],
                             reader[2],
                             reader[3]);
            memcpy(resolved_addr->ipv4, reader, 4);
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
