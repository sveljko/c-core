/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_ADVANCED_HISTORY
#define INC_PUBNUB_ADVANCED_HISTORY

#include "pubnub_memory_block.h"

struct pubnub_chan_msg_count {
    pubnub_chamebl_t channel;
    size_t message_count;
};

/** Extracts 'error_message' attribute value from the transaction response on the
    context @p pb into @p o_msg.
    Can be called for any response, if json object, in case server reported an error 
    @retval 0 error message successfully picked up
    @retval -1 on error(not found, or transaction still in progress on the context)
 */
int pubnub_get_error_message(pubnub_t* pb, pubnub_chamebl_t* o_msg);

/** Parses server response on 'message_counts' operation request and prepares
    msg offset for reading the content of json object for 'channels' key containing
    '"channel":nessage_count' pairs.
    @retval PNR_OK no error reported(parsing successful)
    @retval PNR_FORMAT_ERROR something's wrong with message format
    @retval PNR_ERROR_ON_SERVER server reported an error
 */
enum pubnub_res pbcc_parse_message_counts_response(struct pbcc_context* p);

/** If successful returns number of memebers(key:value pairs) of JSON object
    'channels', or -1 on error(transaction still in progress, or so)
 */
int pubnub_get_chan_msg_counts_size(pubnub_t* pb);

enum pubnub_res pbcc_message_counts_prep(struct pbcc_context* p,
                                         char const*          channel,
                                         char const*          timetoken,
                                         char const*          channel_timetokens);

/** Starts the transaction 'pubnub_message_counts' on the context @p pb for the
    list of channels @p channel for unread messages counts staring from @p timeoken,
    or (exclusive or) list of @p channel_timetokens(corresponding to the list
    'channel' respectively).
    @retval PNR_STARTED request has been sent but transaction still in progress
    @retval PNR_IN_PROGRESS can't start transaction because previous one is still
                            in progress(hasn't finished yet)
    @retval PNR_OK transaction is successfully accomplished and ready for analisis
    @retval PNR_INVALID_CHANNEL
    @retval PNR_INVALID_PARAMETERS on invalid parameters
    @retval PNR_RX_BUFF_NOT_EMPTY if buffer on the previous opreratin left unread
                                  in full
    @return some othres from the same "Pubnub result" enum(You'll know them
            when you see them).
 */
enum pubnub_res pubnub_message_counts(pubnub_t*   pb,
                                      char const* channel, 
                                      char const* timetoken, 
                                      char const* channel_timetokens);

/** On input, @p io_count is the number of taken "counters per channel"(array dimension
    of @p chan_msg_counters). On output(@p io_count), number of "written"(counters per
    channel) in that array (how many there are in the answer). If there are more in the
    answer than there are taken("can't fit all"), wan't be considered an error. It will
    be reported as PUBNUB_LOG_DEBUG().
    @retval 0 on success
    @retval -1 on error(transactin in progress, or format error)
 */
int pubnub_get_chan_msg_counts(pubnub_t* pb, 
                               size_t* io_count, 
                               struct pubnub_chan_msg_count* chan_msg_counters);

/** Array dimension for @p o_count is the number of channels from channel list
    @p channel and it(array 'o_count') has to be provided by the user.
    Message counts oreder in `o_count` is corresponding to channel order in `channel`
    list respectivly, even if the answer itself is different. If there are no requested
    channels in the answer, in 'o_count" array 0 is written at the respective member. 
    If there is a channel name in the answer, not to be found in the `channel` list,
    that won't be considered an error. It will be reported as PUBNUB_LOG_DEBUG().
    @retval 0 on success
    @retval -1 on error(transactin in progress, or format error)
  */
int pubnub_get_message_counts(pubnub_t* pb, char const*channel, size_t* o_count);


#endif /* !defined INC_PUBNUB_ADVANCED_HISTORY */
