
#include "pubnub_internal.h"

#include "core/pubnub_assert.h"
#include "lib/miniz/miniz_tdef.h"
#include "lib/pbcrc32.h"
#include "core/pubnub_log.h"

#define FIRST_TEN_RESERVED_BYTES 10
#define LAST_EIGHT_RESERVED_BYTES 8
#define PUBNUB_MINIMAL_ACCEPTABLE_COMPRESSION_RATIO 0.1

static enum pubnub_res deflate_total_to_context_buffer(pubnub_t*   pb,
                                                       char const* message,
                                                       size_t      message_size)
{
	size_t           unpacked_size = message_size;
    size_t           compressed = sizeof pb->core.gzip_msg_buf -
		                          (FIRST_TEN_RESERVED_BYTES + LAST_EIGHT_RESERVED_BYTES);
	char*            gzip_msg_buf = pb->core.gzip_msg_buf;
	tdefl_compressor comp;

	tdefl_init(&comp, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES);
	tdefl_status status = tdefl_compress(&comp,
										 message,
										 &message_size,
										 gzip_msg_buf + FIRST_TEN_RESERVED_BYTES,
										 &compressed,
										 TDEFL_FINISH);
    switch (status) {
    case TDEFL_STATUS_DONE:
		if (message_size == unpacked_size) {
			uint32_t crc;
			size_t packed_size = FIRST_TEN_RESERVED_BYTES + compressed + LAST_EIGHT_RESERVED_BYTES;
			long int diff = unpacked_size - packed_size;

			PUBNUB_LOG_TRACE("pbgzip_compress(pb=%p) - Length after compression: %zu bytes - "
							 "comression ratio=%lf\n",
							 pb,
							 packed_size,
							 (double)diff/unpacked_size);
			if ((double)diff/unpacked_size <= PUBNUB_MINIMAL_ACCEPTABLE_COMPRESSION_RATIO) {
				/* With insufficient comression we choose not to pack */
				break;
			}
			/* Ciclic redundancy checksum data(little endian) */
			pbcrc32(message, unpacked_size, &crc);
			gzip_msg_buf[packed_size - 5] = crc >> 24;
			gzip_msg_buf[packed_size - 6] = (crc >> 16) & 0xFF;
			gzip_msg_buf[packed_size - 7] = (crc >> 8) & 0xFF;
			gzip_msg_buf[packed_size - 8] = crc & 0xFF;
			/* Unpacked message size is placed at the end of the 'gzip' formated message
			   in the last four bytes(little endian)
			 */
			gzip_msg_buf[packed_size - 1] = (uint32_t)unpacked_size >> 24;
			gzip_msg_buf[packed_size - 2] = ((uint32_t)unpacked_size >> 16) & 0xFF;
			gzip_msg_buf[packed_size - 3] = ((uint32_t)unpacked_size >> 8) & 0xFF;
			gzip_msg_buf[packed_size - 4] = (uint32_t)unpacked_size & 0xFF;
			pb->core.gzip_msg_len = packed_size;
			return PNR_OK;
		}
		else {
            PUBNUB_LOG_ERROR("'Tdef' - Hasn't compressed entire message: %zu bytes compressed,"
							 "unpacked_size=%zu\n",
							 message_size,
							 unpacked_size);
		}
		break;
	case TDEFL_STATUS_BAD_PARAM:
		PUBNUB_LOG_ERROR("'Tdef'- comression failed : bad parameters\n");
        break;
    case TDEFL_STATUS_OKAY:
	case TDEFL_STATUS_PUT_BUF_FAILED:
        PUBNUB_LOG_ERROR("'Tdef'- comression failed : buffer to small\n");
        break;
    default:
        if (status < 0) {
            PUBNUB_LOG_ERROR("Compression failed(Status: %d)\n", status);
        }
        else {
            PUBNUB_LOG_ERROR("'Tdef'- compression status: %d!\n", status);
        }
        break;
	}
    return PNR_BAD_COMPRESSION_FORMAT;
}

enum pubnub_res pbgzip_compress(pubnub_t* pb, char const* message)
{
    char* data = pb->core.gzip_msg_buf;
	size_t size;

	PUBNUB_ASSERT_OPT(message != NULL);
	PUBNUB_ASSERT_OPT(
		sizeof pb->core.gzip_msg_buf > (FIRST_TEN_RESERVED_BYTES + LAST_EIGHT_RESERVED_BYTES));
	/* Gzip format */
    data[0] = 0x1f;
	data[1] = 0x8b; 
	/* Deflate algorithm */
    data[2] = 8;
	/* flags: no file_name, no f_extras, no f_comment, no f_hcrc */
	memset(data + 3, '\0', 7);
	size = strlen(message);
    PUBNUB_LOG_TRACE("pbgzip_compress(pb=%p) - Length before compression:%zu bytes\n", pb, size);

    return deflate_total_to_context_buffer(pb, message, size);
}
