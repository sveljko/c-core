/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_URL_ENCODE
#define INC_PUBNUB_URL_ENCODE

#include "pubnub_config.h"

/** Url-encodes string @p what to user provided @p buffer which should be equal, or greater
    in size than maximum allowed url-encoded channel string size(see 'pubnub_config.h') and
    returns pointer to the same buffer.
    If the buffer provided is smaller than required, function has undefined behaviour.
    If url-encoded representation has size greater than maximum allowed mentioned above,
    this representation is stored in the buffer truncated.
 */
char* pubnub_url_encode(char* buffer, char const* what);

#endif /* !defined INC_PUBNUB_URL_ENCODE */

