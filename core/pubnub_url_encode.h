/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_URL_ENCODE
#define INC_PUBNUB_URL_ENCODE

#include "pubnub_config.h"
#include "pubnub_api_types.h"

/** Url-encodes string @p what to user provided @p buffer which should be equal, or greater
    in size than maximum allowed url-encoded channel string size(see 'pubnub_config.h').
    If the buffer provided is smaller than required, function has undefined behaviour.
    @retval PNR_OK on success,
    @retval PNR_URL_ENCODED_CHANNEL_TOO_LONG on error
 */
enum pubnub_res pubnub_url_encode(char* buffer, char const* what);

#endif /* !defined INC_PUBNUB_URL_ENCODE */

