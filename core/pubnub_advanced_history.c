/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_internal.h"

#if PUBNUB_USE_ADVANCED_HISTORY
#include "pubnub_advanced_history.h"
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
        o_msg->ptr = (char*)found.start;
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
    for (;next < end; next++) {
        if (',' == *next) {
            ++number_of_key_value_pairs;
        }
    }
    ++number_of_key_value_pairs;
    
    pubnub_mutex_unlock(pb->monitor);
    return number_of_key_value_pairs;
}


static enum pubnub_parameter_error check_timetoken(struct pbcc_context* p,
                                                   char const* timetoken)
{
    size_t len = strlen(timetoken);
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
    return pnarg_PARAMS_OK;
}


static enum pubnub_parameter_error check_channel_timetokens(struct pbcc_context* p,
                                                            char const* channel,
                                                            char const* channel_timetokens)
{
    size_t len;
    char const* previous_channel = channel;
    char const* previous_timetoken = channel_timetokens;
    char const* next = strchr(previous_channel, ',');
    char const* next_within_timetokens = strchr(previous_timetoken, ',');
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
    return pnarg_PARAMS_OK;
}

static enum pubnub_parameter_error check_parameters(struct pbcc_context* p,
                                                    char const* channel,
                                                    char const* timetoken,
                                                    char const* channel_timetokens)
{
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
    if ((timetoken != NULL) && (check_timetoken(p, timetoken) != pnarg_PARAMS_OK)) {
        return pnarg_INVALID_TIMETOKEN;  
    }
    if (channel_timetokens != NULL) {
        enum pubnub_parameter_error rslt = check_channel_timetokens(p, channel, channel_timetokens);
        if (rslt != pnarg_PARAMS_OK) {
            return rslt;
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
    
    if ((NULL == channel) ||
        (check_parameters(p, channel, timetoken, channel_timetokens) != pnarg_PARAMS_OK)) {
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
    ch_start = pbjson_skip_whitespace(ch_start, end);
    while ((ch_start < end) && (count < *io_count)) {
        char const* ch_end;

        ch_end = pbjson_find_end_element(ch_start, end);
        chan_msg_counters[count].channel.size = ch_end - ch_start - 1;
        chan_msg_counters[count].channel.ptr = (char*)(ch_start + 1),
        ch_start = pbjson_skip_whitespace(ch_end + 1, end);
        if (*ch_start != ':') {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_chan_msg_counts(pb=%p) - "
                             "colon missing after channel name=%*.s\n"
                             "characters after channel name='%s'",
                             pb,
                             (int)chan_msg_counters[count].channel.size,
                             chan_msg_counters[count].channel.ptr,
                             ch_start);
        }
        else if (sscanf(++ch_start, "%u", (unsigned*)&(chan_msg_counters[count].message_count)) != 1) {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_chan_msg_counts(pb=%p) - "
                             "failed to read the message count for channel='%*.s'\n"
                             "got these characters instead='%s'\n",
                             pb,
                             (int)chan_msg_counters[count].channel.size,
                             chan_msg_counters[count].channel.ptr,                             
                             ch_start - 1);
        }
        else {
            ++count;
        }
        while((*ch_start != ',') && (ch_start < end)) {
            ch_start++;
        }
        if (ch_start == end) {
            break;
        }
        ch_start = pbjson_skip_whitespace(ch_start + 1, end);
    }
    if ((count == *io_count) && (ch_start != end)) {
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


static int initialize_msg_counters(char const* channel, int* o_count)
{
    int         n = 0;
    char const* next;

    PUBNUB_ASSERT_OPT(channel != NULL);
    PUBNUB_ASSERT_OPT(o_count != NULL);
    
    next = channel;
    while (' ' == *next) {
        next++;
    }
    o_count[n++] = -(next - channel) - 1;    
    for (next = strchr(next, ','); next != NULL; next = strchr(next + 1, ','), n++) {
        ++next;
        while (' ' == *next) {
            next++;
        }
        /* Saving negative channel offsets(-1) in the array of counters.
           That is, if channel name from the 'channel' list is not found in the answer
           corresponding array member stays negative, while when channel name(from the
           'channel' list) is found in the response this value is used for locating
           channel name within the list od channels before it is changed to its
           corresponding message counter value(which could, also, be zero).
         */
        o_count[n] = -(next - channel) - 1;
    }
    return n;
}


int pubnub_get_message_counts(pubnub_t* pb, char const* channel, int* o_count)
{
    char const* ch_start;
    char*       end;
    int         n;
    int         counts = 0;

    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_message_counts(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }

    n = initialize_msg_counters(channel, o_count);
    ch_start = pb->core.http_reply + pb->core.msg_ofs;
    end = pb->core.http_reply + pb->core.msg_end + 1;
    if (ch_start >= end) {
        /* Response message carries no counters */
        pubnub_mutex_unlock(pb->monitor);
        return 0;
    }
    ch_start = pbjson_skip_whitespace(ch_start, end);
    while((ch_start < end) && (counts < n)) {
        /* index in the array of message counters */
        int         i = 0;
        /* channel name length */
        unsigned    len;
        char const* ptr_ch;
        char*       ch_end;
        ch_end = (char*)pbjson_find_end_element(ch_start++, end);
        len = ch_end - ch_start;
        for (i = 0, ptr_ch = channel - o_count[0] - 1; i < n ; i++) {
            /* Comparing channel name found in response message with name from 'channel' list. */
            if ((memcmp(ptr_ch, ch_start, len) == 0) &&
                ((*(ptr_ch + len) == ',') || (*(ptr_ch + len) == ' '))) {
                break;
            }
            ptr_ch = channel - o_count[i] - 1;
        }
        if (i == n) {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_msg_counts(pb=%p) - "
                             "channel not present in the query list 'channel',\n"
                             "unhandled channel from the response='%*.s'\n",
                             pb,
                             len,
                             ch_start);
        }
        ch_start = pbjson_skip_whitespace(ch_end + 1, end);
        if (*ch_start != ':') {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_msg_counts(pb=%p) - "
                             "colon missing after channel name='%*.s'\n"
                             "characters after channel name='%s'",
                             pb,
                             len,
                             ptr_ch,
                             ch_start);
        }    /* Saving message count value in the array provided */
        else if (sscanf(++ch_start, "%u", (unsigned*)(o_count + i)) != 1) {
            PUBNUB_LOG_DEBUG("Note: pubnub_get_msg_counts(pb=%p) - "
                             "failed to read the message count for channel='%*.s'\n"
                             "got these characters instead='%s'\n",
                             pb,
                             len,
                             ptr_ch,
                             ch_start - 1);
        }
        else {
            ++counts;
        }
        while((*ch_start != ',') && (ch_start < end)) {
            ch_start++;
        }
        if (ch_start == end) {
            break;
        }
        ch_start = pbjson_skip_whitespace(ch_start + 1, end);
    }
    if ((counts == n) && (ch_start != end)) {
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
