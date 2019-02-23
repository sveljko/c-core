/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_internal.h"

#if PUBNUB_USE_ADVANCED_HISTORY
#include "pubnub_version.h"
#include "pubnub_json_parse.h"
#include "pubnub_url_encode.h"

#include "pubnub_assert.h"
#include "pubnub_log.h"


#define PUBNUB_MIN_TIMETOKEN_LEN 4


/** Should be called only if server reported an error */
int pubnub_get_error_message(pubnub_t* pb, pubnub_chamebl_t* o_msg)
{
    enum pbjson_object_name_parse_result jpresult;
    struct pbjson_elem                   el;
    struct pbjson_elem                   found;
    int                                  rslt;
    
    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_error_message(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    
    el.start    = pb->core.http_reply;
    el.end      = pb->core.http_reply + pb->core.http_buf_len;
    
    jpresult = pbjson_get_object_value(&el, "error_message", &found);
    if (jonmpOK == jpresult) {
        o_msg->size = found.end - found.start + 1;
        memcpy(o_msg->ptr, found.start, o_msg->size);
        rslt = 0;
    }
    else {
        PUBNUB_LOG_ERROR("Error: pubnub_get_error_message(pb=%p) - "
                         "No error message found. error=%d\n"
                         "response=%.*s",
                         pb,
                         jpresult,
                         pb->core.http_buf_len,
                         pb->core.http_reply);
        rslt = -1;
    }

    pubnub_mutex_unlock(pb->monitor);
    return rslt;
}


enum pubnub_res pbcc_parse_message_counts_response(struct pbcc_context* p)
{
    enum pbjson_object_name_parse_result jpresult;
    struct pbjson_elem                   el;
    struct pbjson_elem                   found;
    char*                                reply = p->http_reply;
    int                                  replylen = p->http_buf_len;

    if ((reply[0] != '{') || (reply[replylen - 1] != '}')) {
        PUBNUB_LOG_ERROR("Error: pbcc_parse_message_counts_response(pbcc=%p) - "
                         "Response is not json object: response='%.*s'\n",
                         p,
                         replylen,
                         reply);
        return PNR_FORMAT_ERROR;
    }
    el.start = reply;
    el.end   = reply + replylen;
    jpresult = pbjson_get_object_value(&el, "error", &found);
    if (jonmpOK == jpresult) {
        if (pbjson_elem_equals_string(&found, "false")) {
            jpresult = pbjson_get_object_value(&el, "channels", &found);
            if (jonmpOK == jpresult) {
                if ((*found.start != '{') || (found.end[-1] != '}')) {
                    PUBNUB_LOG_ERROR("Error: pbcc_parse_message_counts_response(pbcc=%p) - "
                                     "Array 'channels' in response is not a json object\n"
                                     "'channels'='%.*s'\n",
                                     p,
                                     (int)(found.end - found.start),
                                     found.start);
                    return PNR_FORMAT_ERROR;
                }
                p->msg_ofs = found.start - reply + 1;
                p->msg_end = found.end - reply - 1;
            }
            else {
                PUBNUB_LOG_ERROR("Error: pbcc_parse_message_counts_response(pbcc=%p) - "
                                 "No 'channels' array found, error=%d\n",
                                 p,
                                 jpresult);
                return PNR_FORMAT_ERROR;
            }
        }
        else {
            return PNR_ERROR_ON_SERVER;        
        }
    }
    else {
        PUBNUB_LOG_ERROR("Error: pbcc_parse_message_counts_response(pbcc=%p) - "
                         "'error' atribute not found in the response. error=%d\n"
                         "response='%.*s'\n",
                         p,
                         jpresult,
                         replylen,
                         reply);
        return PNR_FORMAT_ERROR;
    }
    p->chan_ofs = p->chan_end = 0;

    return PNR_OK;
}


int pubnub_get_chan_msg_counts_size(pubnub_t* pb)
{
    char* next;
    char* end;
    /* Number of '"channel":number_of_unread_msgs' pairs */
    int number_of_key_value_pairs = 0;
    
    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_chan_msg_counts_size(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    end = pb->core.http_reply + pb->core.msg_end;
    next = pb->core.http_reply + pb->core.msg_ofs;
    if (next >= end) {
        pubnub_mutex_unlock(pb->monitor);
        return 0;
    }
    /* replacing json object end bracket with string end('\0') */
    *(end + 1) = '\0';
    next = strchr(next, ',');
    while (next != NULL) {
        ++number_of_key_value_pairs;
        next = strchr(next + 1, ',');
    }
    ++number_of_key_value_pairs;
    
    pubnub_mutex_unlock(pb->monitor);
    return number_of_key_value_pairs;
}


static enum pubnub_parameter_error check_parameters(struct pbcc_context* p,
                                                    char const* channel,
                                                    char const* timetoken,
                                                    char const* channel_timetokens)
{
    char const* next;
    char const* next_within_timetokens;
    char const* previous_channel;
    char const* previous_timetoken;
    /** Length of 'this' and 'that' */
    size_t len;

    if ((timetoken != NULL) && (channel_timetokens != NULL)) {
        PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                         "Both 'timetoken' and 'channel_timetokens'"
                         "present as non NULL parameters.\n"
                         "timetoken='%s'\n"
                         "channel_timetokens=%s\n",
                         p,
                         timetoken,
                         channel_timetokens);
        return pnarg_PRESENT_EXCLUSIVE_ARGUMENTS;
    }
    if (timetoken != NULL) {
        len = strlen(timetoken);
        if (len < PUBNUB_MIN_TIMETOKEN_LEN) {
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                             "Timetoken shorter than minimal permited.\n"
                             "PUBNUB_MIN_TIMETOKEN_LEN=%d\n"
                             "timetoken='%s'",
                             p,
                             PUBNUB_MIN_TIMETOKEN_LEN,
                             timetoken);
            return pnarg_INVALID_TIMETOKEN;
        }
        else if (strspn(timetoken, OK_SPAN_CHARACTERS) != len) {
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                             "Invalid timetoken.(timetoken='%s')\n",
                             p,
                             timetoken);
            return pnarg_INVALID_TIMETOKEN;
        }
    }
    if (channel_timetokens != NULL) {
        previous_channel = channel;
        previous_timetoken = channel_timetokens;
        next = strchr(previous_channel, ',');
        next_within_timetokens = strchr(previous_timetoken, ',');
        while (next != NULL) {
            if (NULL == next_within_timetokens) {                
                PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - Number of channels and "
                                 "number of channel_timetokens don't match: "
                                 "More channels than channel_timetokens.\n"
                                 "channel='%s'\n"
                                 "channel_timetokens='%s')\n",
                                 p,
                                 channel,
                                 channel_timetokens);
                return pnarg_CHANNEL_TIMETOKEN_COUNT_MISMATCH;
            }
            if ((next - previous_channel > PUBNUB_MAX_CHANNEL_NAME_LENGTH) ||
                (next - previous_channel < 1)) {
                PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                                 "Channel name within the channel list too long(or missing):\n"
                                 "PUBNUB_MAX_CHANNEL_NAME_LENGTH=%d\n"
                                 "channel='%.*s'\n",
                                 p,
                                 PUBNUB_MAX_CHANNEL_NAME_LENGTH,
                                 (int)(next - previous_channel),
                                 previous_channel);
                return pnarg_INVALID_CHANNEL;
            }
            if (next_within_timetokens - previous_timetoken < PUBNUB_MIN_TIMETOKEN_LEN) {
                PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                                 "Timetoken within the list shorter than minimal permited.\n"
                                 "PUBNUB_MIN_TIMETOKEN_LEN=%d\n"
                                 "timetoken='%.*s'",
                                 p,
                                 PUBNUB_MIN_TIMETOKEN_LEN,
                                 (int)(next_within_timetokens - previous_timetoken),
                                 previous_timetoken);
                return pnarg_INVALID_TIMETOKEN;
            }
            previous_channel = next + 1;
            previous_timetoken = next_within_timetokens + 1;
            next = strchr(previous_channel, ',');
            next_within_timetokens = strchr(previous_timetoken, ',');
        }
        if (next_within_timetokens != NULL) {                
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - Number of channels and "
                             "number of channel_timetokens don't match: "
                             "More channel_timetokens than channels.\n"
                             "channel='%s'\n"
                             "channel_timetokens='%s')\n",
                             p,
                             channel,
                             channel_timetokens);
            return pnarg_CHANNEL_TIMETOKEN_COUNT_MISMATCH;
        }
        len = strlen(previous_channel);
        if ((len > PUBNUB_MAX_CHANNEL_NAME_LENGTH) || (len < 1)) {
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                             "Channel name at the end of the channel list is too long(or missing):\n"
                             "PUBNUB_MAX_CHANNEL_NAME_LENGTH=%d\n"
                             "channel='%s'\n",
                             p,
                             PUBNUB_MAX_CHANNEL_NAME_LENGTH,
                             previous_channel);
            return pnarg_INVALID_CHANNEL;
        }
        if (strlen(previous_timetoken) < PUBNUB_MIN_TIMETOKEN_LEN) {
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - "
                             "Timetoken at the end of the list is shorter than minimal permited.\n"
                             "PUBNUB_MIN_TIMETOKEN_LEN=%d\n"
                             "timetoken='%s'",
                             p,
                             PUBNUB_MIN_TIMETOKEN_LEN,
                             previous_timetoken);
            return pnarg_INVALID_TIMETOKEN;
        }
        if (strspn(channel_timetokens, OK_SPAN_CHARACTERS) != strlen(channel_timetokens)) {
            PUBNUB_LOG_ERROR("Error: message_counts_prep(pbcc=%p) - Invalid channel_timetokens."
                             "(channel_timetokens='%s')\n",
                             p,
                             channel_timetokens);
            return pnarg_INVALID_TIMETOKEN;
        }
    }

    return pnarg_PARAMS_OK;
}


enum pubnub_res pbcc_message_counts_prep(struct pbcc_context* p,
                                         char const*          channel,
                                         char const*          timetoken,
                                         char const*          channel_timetokens)
{
    char const* const uname = pubnub_uname();
    char const*       uuid  = pbcc_uuid_get(p);
    
    if (NULL == channel) {
        return PNR_INVALID_CHANNEL;
    }
    if (check_parameters(p, channel, timetoken, channel_timetokens) != pnarg_PARAMS_OK) {
        return PNR_INVALID_PARAMETERS;
    }
    if (p->msg_ofs < p->msg_end) {
        return PNR_RX_BUFF_NOT_EMPTY;
    }

    p->http_content_len = 0;
    p->msg_ofs = p->msg_end = 0;

    p->http_buf_len = snprintf(p->http_buf,
                               sizeof p->http_buf,
                               "/v3/history/sub-key/%s/channels-with-messages/",
                               p->subscribe_key);
    APPEND_URL_ENCODED_M(p, channel);
    APPEND_URL_PARAM_M(p, "pnsdk", uname, '?');
    APPEND_URL_PARAM_M(p, "auth", p->auth, '&');
    APPEND_URL_PARAM_M(p, "uuid", uuid, '&');
    APPEND_URL_PARAM_M(p, "timetoken", timetoken, '&');
    APPEND_URL_PARAM_M(p, "channelTimetokens", channel_timetokens, '&');

    return PNR_STARTED;
}


enum pubnub_res pubnub_message_counts(pubnub_t*   pb,
                                      char const* channel, 
                                      char const* timetoken, 
                                      char const* channel_timetokens)
{
    enum pubnub_res rslt;

    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        return PNR_IN_PROGRESS;
    }

    rslt = pbcc_message_counts_prep(&pb->core, channel, timetoken, channel_timetokens);
    if (PNR_STARTED == rslt) {
        pb->trans            = PBTT_MESSAGE_COUNTS;
        pb->core.last_result = PNR_STARTED;
        pbnc_fsm(pb);
        rslt = pb->core.last_result;
    }

    pubnub_mutex_unlock(pb->monitor);
    return rslt;
}


int pubnub_get_chan_msg_counts(pubnub_t* pb, 
                               size_t* io_count, 
                               struct pubnub_chan_msg_count* chan_msg_counters)
{
    char const*  ch_start;
    char*  end;
    size_t count = 0;

    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));
    PUBNUB_ASSERT_OPT(io_count != NULL);
    PUBNUB_ASSERT_OPT(chan_msg_counters != NULL);

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_chan_msg_counts_size(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    ch_start = pb->core.http_reply + pb->core.msg_ofs;
    end = pb->core.http_reply + pb->core.msg_end + 1;
    if (ch_start >= end) {
        *io_count = 0;
        pubnub_mutex_unlock(pb->monitor);
        return 0;
    }
    /* replacing json object end bracket with string end('\0') */
    *end = '\0';
    ch_start = pbjson_skip_whitespace(ch_start, end);
    while((*ch_start != '\0') && (count < *io_count)) {
        char const* ch_end;

        ch_end = pbjson_find_end_element(ch_start, end);
        chan_msg_counters[count].channel.size = ch_end - ch_start - 1;
        memcpy(chan_msg_counters[count].channel.ptr,
               ch_start + 1,
               chan_msg_counters[count].channel.size);
        ch_start = pbjson_skip_whitespace(ch_end + 1, end);
        if (*ch_start != ':') {
            PUBNUB_LOG_ERROR("Error: pubnub_get_chan_msg_counts(pb=%p) - "
                             "colon missing after channel name\n"
                             "characters after channel name='%s'",
                             pb,
                             ch_start);
            pubnub_mutex_unlock(pb->monitor);
            return -1;
        }
        if (sscanf(++ch_start, "%u", (unsigned*)&(chan_msg_counters[count].message_count)) != 1) {
            PUBNUB_LOG_ERROR("Error: pubnub_get_chan_msg_counts(pb=%p) - "
                             "failed to read the message count.\n"
                             "got these characters instead='%s'\n",
                             pb,
                             ch_start);
            pubnub_mutex_unlock(pb->monitor);
            return -1;
        }
        ++count;
        ch_start = strchr(ch_start, ',');
        if (NULL == ch_start) {
            ch_start = end;
            break;
        }
        ch_start = pbjson_skip_whitespace(ch_start + 1, end);
    }
    if ((count == *io_count) && (*ch_start != '\0')) {
        PUBNUB_LOG_DEBUG("Note: pubnub_get_chan_msg_counts(pb=%p) - "
                         "more than expected message counters,\n"
                         "unhandled part of the response='%s'\n",
                         pb,
                         ch_start);
    }
    else {
        *io_count = count;
    }
    pb->core.msg_ofs = ch_start - pb->core.http_reply;
    
    pubnub_mutex_unlock(pb->monitor);
    return 0;
}


int pubnub_get_message_counts(pubnub_t* pb, char const*channel, size_t* o_count)
{
    char const* ch_start;
    char*       end;
    int         n = 0;
    int         counts = 0;
    char const* next;

    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));
    PUBNUB_ASSERT_OPT(channel != NULL);
    PUBNUB_ASSERT_OPT(o_count != NULL);

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_message_counts(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    /* Initilazing all counters to zeros */
    for (next = strchr(channel, ','); next != NULL; next = strchr(next + 1, ','), n++) {
        o_count[n] = 0;
    }
    o_count[n++] = 0;

    ch_start = pb->core.http_reply + pb->core.msg_ofs;
    end = pb->core.http_reply + pb->core.msg_end + 1;
    if (ch_start >= end) {
        /* Response message carries no counters */
        pubnub_mutex_unlock(pb->monitor);
        return 0;
    }
    /* replacing json object end bracket with string end('\0') */
    *end = '\0';
    ch_start = pbjson_skip_whitespace(ch_start, end);
    while((*ch_start != '\0') && (counts < n)) {
        /* index in the array of message counters */
        int         i = 0;
        /* channel name length */
        unsigned    len;
        char const* ptr_ch;
        char*       ch_end;
        ch_end = (char*)pbjson_find_end_element(ch_start++, end);
        /* Preparing channel found in response message for search in @p 'channel' list,
           changing its quotation mark into string end('\0')
         */
        *ch_end = '\0';
        len = ch_end - ch_start;
        for (ptr_ch = strstr(channel, ch_start); ptr_ch != NULL;) {
            if (((',' == *(ptr_ch - 1)) || (' ' == *(ptr_ch - 1)))
                && ((',' == *(ptr_ch + len)) || (' ' == *(ptr_ch + len)))) {
                /* Finding the channel index in the @p channel list */
                for (next = strchr(channel, ','); next != NULL; next = strchr(next + 1, ','), i++) {
                    if (next > ptr_ch) {
                        break;
                    }
                }                
                break;
            }
            ptr_ch = strstr(ptr_ch + len, ch_start);
        }
        if (NULL == ptr_ch) {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_msg_counts(pb=%p) - "
                             "channel not present in the query list 'channel',\n"
                             "unhandled part of the response='%s'\n",
                             pb,
                             ch_start);
        }
        /* Returning end character to its original quotation mark('\"') */
        *ch_end = '\"';
        ch_start = pbjson_skip_whitespace(ch_end + 1, end);
        if (*ch_start != ':') {
            PUBNUB_LOG_ERROR("Error: pubnub_get_msg_counts(pb=%p) - "
                             "colon missing after channel name\n"
                             "characters after channel name='%s'",
                             pb,
                             ch_start);
            pubnub_mutex_unlock(pb->monitor);
            return -1;
        }
        /* Saving messages count value found within array provided */
        if (sscanf(++ch_start, "%u", (unsigned*)(o_count + i)) != 1) {
            PUBNUB_LOG_ERROR("Error: pubnub_get_msg_counts(pb=%p) - "
                             "failed to read the message count.\n"
                             "got these characters instead='%s'\n",
                             pb,
                             ch_start);
            pubnub_mutex_unlock(pb->monitor);
            return -1;
        }
        ++counts;
        ch_start = strchr(ch_start, ',');
        if (NULL == ch_start) {
            ch_start = end;
            break;
        }
        ch_start = pbjson_skip_whitespace(ch_start + 1, end);
    }
    if ((counts == n) && (*ch_start != '\0')) {
        PUBNUB_LOG_DEBUG("Note: pubnub_get_msg_counts(pb=%p) - "
                         "more than expected message counters,\n"
                         "unhandled part of the response='%s'\n",
                         pb,
                         ch_start);
    }
    pb->core.msg_ofs = ch_start - pb->core.http_reply;
    
    pubnub_mutex_unlock(pb->monitor);
    return 0;
}

#endif /* PUBNUB_USE_ADVANCED_HISTORY */
