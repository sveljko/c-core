/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_internal.h"

#include "pubnub_url_encode.h"

#include <string.h>

char* pubnub_url_encode(char* buffer, char const* what)
{
    unsigned i = 0;
    bool forced_finish = false;

    PUBNUB_ASSERT_OPT(buffer != NULL);
    PUBNUB_ASSERT_OPT(what != NULL);

    while (what[0]) {
        /* RFC 3986 Unreserved characters plus few
         * safe reserved ones. */
        size_t okspan = strspn(
            what,
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~"
            ",=:;@[]");
        if (okspan > 0) {
            if (okspan >= PUBNUB_MAX_URL_ENCODED_CHANNEL - i - 1) {
                okspan = PUBNUB_MAX_URL_ENCODED_CHANNEL - i - 1;
                forced_finish = true;
            }
            memcpy(buffer + i, what, okspan);
            i += okspan;
            buffer[i] = '\0';
            if (forced_finish) {
                break;
            }
            what += okspan;
        }
        if (what[0]) {
            /* %-encode a non-ok character. */
            char enc[4] = { '%' };
            enc[1]      = "0123456789ABCDEF"[(unsigned char)what[0] / 16];
            enc[2]      = "0123456789ABCDEF"[(unsigned char)what[0] % 16];
            if (3 > PUBNUB_MAX_URL_ENCODED_CHANNEL - i - 1) {
                /* If the character encoded has no room in the buffer, we'll excuse it
                   and finish.
                 */
                break;
            }
            /* Last copied character is '\0' */
            memcpy(buffer + i, enc, 4);
            i += 3;
            ++what;
        }
    }

    return buffer;
}
