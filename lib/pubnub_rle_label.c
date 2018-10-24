/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/pubnub_rle_label.h"

#include "core/pubnub_assert.h"
#include "core/pubnub_log.h"

#include <string.h>

/* Maximum number of characters until the next dot('.'), or finishing NULL('\0') in array of bytes */
#define MAX_ALPHABET_STRETCH_LENGTH 63


unsigned char* label_encode(uint8_t* encoded, size_t n, uint8_t const* host)
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
                /* label too long */
                return NULL;
            }

            *lpos = (uint8_t)(d - 1);
            lpos += d;

            if ('\0' == hc) {
                break;
            }
        }
    }

    return encoded;
}


int label_decode(uint8_t*       decoded,
                 size_t         n,
                 uint8_t const* src,
                 uint8_t const* buffer,
                 size_t         buffer_size,
                 size_t*        o_bytes_to_skip)
{
    uint8_t*             dest   = decoded;
    uint8_t const* const end    = decoded + n;
    uint8_t const*       reader = src;

    PUBNUB_ASSERT_OPT(n > 0);
    PUBNUB_ASSERT_OPT(src != NULL);
    PUBNUB_ASSERT_OPT(buffer != NULL);
    PUBNUB_ASSERT_OPT(decoded != NULL);
    PUBNUB_ASSERT_OPT(o_bytes_to_skip != NULL);

    *o_bytes_to_skip = 0;
    while (dest < end) {
        uint8_t b = *reader;
        if (b & 0xC0) {
            uint16_t offset = (b & 0x3F) * 256 + reader[1];
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 2;
            }
            if (offset >= buffer_size) {
                PUBNUB_LOG_ERROR("Error in DNS label/name decoding - offset=%d "
                                 ">= buffer_size=%d\n",
                                 offset,
                                 (int)buffer_size);
                *dest = '\0';
                return -1;
            }
            reader = buffer + offset;
        }
        else if (0 == b) {
            if (0 == *o_bytes_to_skip) {
                *o_bytes_to_skip = reader - src + 1;
            }
            return 0;
        }
        else {
            if (dest != decoded) {
                *dest++ = '.';
            }
            if (dest + b >= end) {
                PUBNUB_LOG_ERROR("Error in DNS label/name decoding - dest=%p + "
                                 "b=%d >= end=%p\n",
                                 dest,
                                 b,
                                 end);
                *dest = '\0';
                return -1;
            }
            memcpy(dest, reader + 1, b);
            dest[b] = '\0';
            dest += b;
            reader += b + 1;
        }
    }

    PUBNUB_LOG_ERROR(
        "Destination for decoding DNS label/name too small, n=%d\n", (int)n);

    return -1;
}
