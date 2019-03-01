/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_ADVANCED_HISTORY
#define INC_PUBNUB_ADVANCED_HISTORY

#include "pbcc_advanced_history.h"

/** Extracts 'error_message' attribute value from the transaction response on the
    context @p pb into @p o_msg.
    Can be called for any response, if it is regular json object, in case
    server reported an error 
    @retval 0 error message successfully picked up
    @retval -1 on error(not found, or transaction still in progress on the context)
 */
int pubnub_get_error_message(pubnub_t* pb, pubnub_chamebl_t* o_msg);

/** If successful returns number of members(key:value pairs) of JSON object
    'channels', or -1 on error(transaction still in progress, or so)
 */
int pubnub_get_chan_msg_counts_size(pubnub_t* pb);

/** Starts the transaction 'pubnub_message_counts' on the context @p pb for the
    list of channels @p channel for unread messages counts starting from @p timeoken,
    or (exclusive or) list of @p channel_timetokens(corresponding to the list
    'channel').
    @retval PNR_STARTED request has been sent, but transaction is still in progress
    @retval PNR_IN_PROGRESS can't start transaction because previous one is still
                            in progress(hasn't finished yet)
    @retval PNR_OK transaction is successfully accomplished and ready for analysis
    @retval PNR_INVALID_PARAMETERS on invalid parameters
    @retval PNR_RX_BUFF_NOT_EMPTY buffer on the previous transaction left unread
                                  in full
    @retval otherwise a result with the same meaning as for any other transaction
 */
enum pubnub_res pubnub_message_counts(pubnub_t*   pb,
                                      char const* channel, 
                                      char const* timetoken, 
                                      char const* channel_timetokens);

/** On input, @p io_count is the number of allocated "counters per channel"(array
    dimension of @p chan_msg_counters). On output(@p io_count), number of counters per
    channel in the answer. If there are more in the answer than there are in the allocated
    array("can't fit all"), wan't be considered an error. It will be reported as
    PUBNUB_LOG_DEBUG().
    @retval 0 on success
    @retval -1 on error(transaction in progress, or format error)
 */
int pubnub_get_chan_msg_counts(pubnub_t* pb, 
                               size_t* io_count, 
                               struct pubnub_chan_msg_count* chan_msg_counters);

/** Array dimension for @p o_count is the number of channels from channel list
    @p channel and it('o_count' array) has to be provided by the user.
    Message counts order in `o_count` is corresponding to channel order in `channel`
    list respectively, even if the answer itself is different. If the requested
    channel(from 'o_count' array) is not found in the answer, the message counter
    in the respective 'o_count' array member has negative value. 
    If there is a channel name in the answer, not to be found in requested
    `channel` list, that won't be considered an error. It will be reported as
    PUBNUB_LOG_DEBUG().
    @retval 0 on success
    @retval -1 on error(transaction in progress, or format error)
  */
int pubnub_get_message_counts(pubnub_t* pb, char const*channel, int* o_count);


#endif /* !defined INC_PUBNUB_ADVANCED_HISTORY */
