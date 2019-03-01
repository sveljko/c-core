/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_internal.h"

#if PUBNUB_USE_ADVANCED_HISTORY
#include "pubnub_advanced_history.h"
#include "pubnub_json_parse.h"

#include "pubnub_assert.h"
#include "pubnub_log.h"


/** Should be called only if server reported an error */
int pubnub_get_error_message(pubnub_t* pb, pubnub_chamebl_t* o_msg)
{
    int rslt;
    
    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_error_message(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    rslt = pbcc_get_error_message(&(pb->core), o_msg);

    pubnub_mutex_unlock(pb->monitor);
    return rslt;
}


int pubnub_get_chan_msg_counts_size(pubnub_t* pb)
{    
    /* Number of '"channel":msg_count' pairs */
    int rslt;

    PUBNUB_ASSERT(pb_valid_ctx_ptr(pb));

    pubnub_mutex_lock(pb->monitor);
    if (!pbnc_can_start_transaction(pb)) {
        pubnub_mutex_unlock(pb->monitor);
        PUBNUB_LOG_ERROR("Error: pubnub_get_chan_msg_counts_size(pb=%p) - "
                         "Transacton in progress on the context",
                         pb);
        return -1;
    }
    rslt = pbcc_get_chan_msg_counts_size(&(pb->core));
    
    pubnub_mutex_unlock(pb->monitor);
    return rslt;
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

    rslt = pbcc_message_counts_prep(&(pb->core), channel, timetoken, channel_timetokens);
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
    int rslt;

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
    rslt = pbcc_get_chan_msg_counts(&(pb->core), io_count, chan_msg_counters);
    
    pubnub_mutex_unlock(pb->monitor);
    return rslt;
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
    if (ch_start >= end - 1) {
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
        for (i = 0; i < n ; i++) {
            ptr_ch = channel - o_count[i] - 1;
            /* Comparing channel name found in response message with name from 'channel' list. */
            if ((memcmp(ptr_ch, ch_start, len) == 0) &&
                ((' ' == *(ptr_ch + len)) || (',' == *(ptr_ch + len)) || ('\0' == *(ptr_ch + len)))) {
                break;
            }
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
