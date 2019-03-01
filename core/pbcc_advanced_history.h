/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if PUBNUB_USE_ADVANCED_HISTORY
#if !defined INC_PBCC_ADVANCED_HISTORY
#define INC_PBCC_ADVANCED_HISTORY

/** @file pbcc_advanced_history.h

    This has the functions for formating and parsing the
    requests and responses for 'advanced history' transactions
*/

#include "pubnub_memory_block.h"

/** Structure containing channel name as char memory block and field with
    message count for messages received on the channel since given point in time
    (defined by parameters passed to the function).
    Used to store information retrieved by 'advanced history' message_counts operation
 */
struct pubnub_chan_msg_count {
    /* Channel name as char memory block */
    pubnub_chamebl_t channel;
    /* Message count for the corresponding channel since given point in time */
    size_t message_count;
};


struct pbcc_context;


/** Parses server response on 'message_counts' transaction request and prepares
    msg offset for reading the content of json object for 'channels' key containing
    '"channel_name":nessage_count' pairs.
    @retval PNR_OK parsing successful
    @retval PNR_FORMAT_ERROR something's wrong with message format
    @retval PNR_ERROR_ON_SERVER server reported an error
 */
enum pubnub_res pbcc_parse_message_counts_response(struct pbcc_context* p);

/** Extracts 'error_message' attribute value from the transaction response on the
    pbcc_context @p p into @p o_msg.
    Can be called for any response, if it is regular json object, in case
    server reported an error 
    @retval 0 error message successfully picked up
    @retval -1 on error
 */
int pbcc_get_error_message(struct pbcc_context* p, pubnub_chamebl_t* o_msg);

/** If successful returns number of members(key:value pairs) of JSON object
    'channels', or -1 on error
 */
int pbcc_get_chan_msg_counts_size(struct pbcc_context* p);

/** On input, @p io_count is the number of allocated "counters per channel"(array
    dimension of @p chan_msg_counters). On output(@p io_count), number of counters per
    channel in the answer. If there are more in the answer than there are in the allocated
    array("can't fit all"), wan't be considered an error. It will be reported as
    PUBNUB_LOG_DEBUG().
    @retval 0 on success
    @retval -1 on error
 */
int pbcc_get_chan_msg_counts(struct pbcc_context* p, 
                             size_t* io_count, 
                             struct pubnub_chan_msg_count* chan_msg_counters);


/** Prepares the 'message_counts' operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_message_counts_prep(struct pbcc_context* p,
                                         char const*          channel,
                                         char const*          timetoken,
                                         char const*          channel_timetokens);
#endif /* INC_PBCC_ADVANCED_HISTORY */
#endif /* PUBNUB_USE_ADVANCED_HISTORY */

