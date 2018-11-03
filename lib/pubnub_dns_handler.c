/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/pubnub_dns_handler.h"

#include "pubnub_internal.h"

#include "core/pubnub_assert.h"
#include "core/pubnub_log.h"

#include <string.h>

/** DNS message header */
struct DNS_HEADER {
    /** identification number */
    uint16_t id;

    /** Options. This is a bitfield, IETF bit-numerated:

       |0 |1 2 3 4| 5| 6| 7| 8| 9|10|11|12 13 14 15|
       |QR|OPCODE |AA|TC|RD|RA| /|AD|CD|   RCODE   |

       - QR query-response bit (0->query, 1->response)
       - OPCODE 0->query, 1->inverse query, 2->status
       - AA authorative answer
       - TC truncation (1 message was truncated, 0 otherwise)
       - RD recursion desired
       - RA recursion available
       - AD authenticated data
       - CD checking disabled
       - RCODE response type 0->no error, 1->format error, 2-> server fail,
       3->name eror, 4->not implemented, 5->refused
     */
    uint16_t options;

    /** number of question entries */
    uint16_t q_count;
    /** number of answer entries */
    uint16_t ans_count;
    /** number of authority entries */
    uint16_t auth_count;
    /** number of resource entries */
    uint16_t add_count;
};

enum DNSoptionsMask {
    dnsoptRCODEmask  = 0x000F,
    dnsoptCDmask     = 0x0010,
    dnsoptADmask     = 0x0020,
    dnsoptRAmask     = 0x0080,
    dnsoptRDmask     = 0x0100,
    dnsoptTCmask     = 0x0200,
    dnsoptAAmask     = 0x0400,
    dnsoptOPCODEmask = 0x7800,
    dnsoptQRmask     = 0x8000,
};


/** Constant sized fields of query structure */
struct QUESTION {
    /* Question type */
    uint16_t qtype;
    /* Question class */
    uint16_t qclass;
};

/** Constant sized fields of the resource record (RR) structure */
#pragma pack(push, 1)
struct R_DATA {
    /** Type of resource record */
    uint16_t type;
    /** Class code */
    uint16_t class_;
    /** Time-To-Live - count of seconds RR stays valid */
    uint32_t ttl;
    /** Length of RDATA (in octets) */
    uint16_t data_len;
};
#pragma pack(pop)

/** Question/query types */
enum DNSqueryType {
    /** Address - IPv4 */
    dnsA     = 1,
    /** Name server */
    dnsNS    = 2,
    /** Canonical name */
    dnsCNAME = 5,
    /** Start of authority */
    dnsSOA   = 6,
    /** Pointer (to another location in the name space ) */
    dnsPTR   = 12,
    /** Mail exchange (responsible for handling e-mail sent to the
     * domain */
    dnsMX    = 15,
    /** IPv6 address - 128 bit */
    dnsAAAA  = 28,
    /** Service locator */
    dnsSRV   = 33,
    /** All cached records */
    dnsANY   = 255
};

/** Question/query class */
enum DNSqclass { dnsqclassInternet = 1 };


/* Maximum number of characters until the next dot('.'), or finishing NULL('\0') in array of bytes */
#define MAX_ALPHABET_STRETCH_LENGTH 63

/* Maximum passes through the loop allowed while decoding label, considering that it may
   contain five to six dots. Defined in order to detect erroneous offset pointers infinite loops. 
*/
#define MAXIMUM_LOOP_PASSES 10

/** Do the label (host) encoding. This strange kind of "run-time
    length encoding" will convert `"www.google.com"` to
    `"\3www\6google\3com"`.
    @param encoded Pointer to the buffer where encoded host label will be placed
    @param n       Maximum buffer length provided
    @param host    Label to encode

    @return Pointer to the encoded label(same as @p encoded) on success, NULL otherwise
 */
static unsigned char* rle_label_encode(uint8_t* encoded, size_t n, uint8_t const* host)
{
    uint8_t*             dest = encoded + 1;
    uint8_t*             lpos;
    uint8_t const* const end = encoded + n;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(host != NULL);
    PUBNUB_ASSERT_OPT(encoded != NULL);

    lpos  = encoded;
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
                                 encoded,
                                 (unsigned)d,
                                 MAX_ALPHABET_STRETCH_LENGTH);
                return NULL;
            }

            *lpos = (uint8_t)(d - 1);
            lpos += d;

            if ('\0' == hc) {
                break;
            }
        }
    }
    if ('\0' != *(dest -1)) {
        PUBNUB_LOG_ERROR("Error: in DNS label/name encoding - "
                         "Buffer for encoded label too small: host='%s', n=%u, encoded='%s'\n",
                         host,
                         (unsigned)n,
                         encoded);
        return NULL;
    }

    return encoded;
}


int pubnub_prepare_dns_request(uint8_t* buf, size_t buf_size, unsigned char* host, int *to_send)
{
    struct DNS_HEADER* dns   = (struct DNS_HEADER*)buf;
    uint8_t*           qname = buf + sizeof *dns;
    struct QUESTION*   qinfo;

    *to_send = 0;
	
    PUBNUB_ASSERT_OPT(buf_size > sizeof *dns);
    dns->id        = htons(33); /* in lack of a better ID */
    dns->options   = htons(dnsoptRDmask);
    dns->q_count   = htons(1);
    dns->ans_count = dns->auth_count = dns->add_count = 0;
    *to_send      += sizeof *dns;

    if(NULL == rle_label_encode(qname, buf_size - sizeof *dns, host)) {
        return -1;
    }
    *to_send      += strlen((char*)qname) + 1;

    PUBNUB_ASSERT_OPT(buf_size - *to_send > sizeof *qinfo);
    qinfo          = (struct QUESTION*)(buf + *to_send);
    qinfo->qtype   = htons(dnsA);
    qinfo->qclass  = htons(dnsqclassInternet);
    *to_send      += sizeof *qinfo;

    return 0;
}


#define RL_DECODIG_FAILS(...) do {                                                 \
                                  PUBNUB_LOG(PUBNUB_LOG_LEVEL_ERROR,               \
                                             "Error: in DNS label/name decoding - "\
                                             __VA_ARGS__);                         \
                                  *dest = '\0';                                    \
                                  return -1;                                       \
                              } while(0)  

/* Do the label decoding. Apart from the RLE decoding of
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
static int rle_label_decode(uint8_t*       decoded,
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
            RL_DECODIG_FAILS("Too many passes(%d) through the loop.\n", pass);
        }
        if (0xC0 == (b & 0xC0)) {
            uint16_t offset = (b & 0x3F) * 256 + reader[1];
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 2;
            }
            if (offset >= buffer_size) {
                RL_DECODIG_FAILS("offset=%d >= buffer_size=%d\n", offset, (int)buffer_size);
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
                RL_DECODIG_FAILS("dest=%p + b=%d >= end=%p - "
                                 "Destination for decoding label/name too small, n=%u\n",
                                 dest,
                                 b,
                                 end,
                                 (unsigned)n);
            }
            if ((unsigned)(reader + b - buffer + 1) > buffer_size) {
                RL_DECODIG_FAILS("About to read outside the buffer:\n"
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
            RL_DECODIG_FAILS("Bad offset format: b & 0xC0=%d, &b=%p\n", b & 0xC0, &b);
        }
    }

    /* If by any chance function reach this point n should be 0, but you just never know */
    PUBNUB_LOG_ERROR("Error: in DNS label/name decoding - "
                     "Destination for decoding label/name too small, n=%u\n", (unsigned)n);
    return -1;
}


int pubnub_pick_resolved_address(uint8_t* buf, int msg_size, struct sockaddr_in* resolved_addr)
{
    struct DNS_HEADER* dns;
    uint8_t*           reader;
    int                i;

    PUBNUB_ASSERT_OPT(buf != NULL);

	dns = (struct DNS_HEADER*)buf;
    reader = buf + sizeof *dns;
    PUBNUB_LOG_TRACE("DNS response has: %hu Questions, %hu Answers, %hu "
                     "Auth. Servers, %hu Additional records.\n",
                     ntohs(dns->q_count),
                     ntohs(dns->ans_count),
                     ntohs(dns->auth_count),
                     ntohs(dns->add_count));
    if (ntohs(dns->q_count) != 1) {
        PUBNUB_LOG_INFO("Strange DNS response, we sent one question, but DNS "
                        "response doesn't have one question.\n");
    }
    for (i = 0; i < ntohs(dns->q_count); ++i) {
        uint8_t name[256];
        size_t  to_skip;

        if (0 != rle_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip)) {
            reader += to_skip + sizeof(struct QUESTION);
            continue;
        }
        PUBNUB_LOG_TRACE(
            "DNS response, %d. question name: %s, to_skip=%d\n", i+1, name, (int)to_skip);

        /* Could check for QUESTION data format (QType and QClass), but
           even if it's wrong, we don't know what to do with it, so,
           there's no use */
        reader += to_skip + sizeof(struct QUESTION);
    }
    for (i = 0; i < ntohs(dns->ans_count); ++i) {
        uint8_t        name[256];
        size_t         to_skip;
        struct R_DATA* prdata;
        size_t         r_data_len;

        if (0 != rle_label_decode(name, sizeof name, reader, buf, msg_size, &to_skip)) {
            /* Even if label decoding fails(having offsets messed up, maybe), hopefully 'to_skip'
               will be set properly and we keep chasing good usable answer
            */
            prdata     = (struct R_DATA*)(reader + to_skip);
            reader += to_skip + sizeof *prdata + ntohs(prdata->data_len);
            continue;
        }
        prdata     = (struct R_DATA*)(reader + to_skip);
        r_data_len = ntohs(prdata->data_len);
        reader += to_skip + sizeof *prdata;

        PUBNUB_LOG_TRACE(
            "DNS %d. answer: %s, to_skip:%zu, type=%hu, data_len=%zu\n",
            i+1,
            name,
            to_skip,
            ntohs(prdata->type),
            r_data_len);

        if (ntohs(prdata->type) == dnsA) {
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
