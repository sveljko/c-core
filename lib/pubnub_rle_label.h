/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_RLE_LABEL
#define      INC_RLE_LABEL

#include <stdint.h>
#include <stddef.h>

/** Do the label (host) encoding. This strange kind of "run-time
    length encoding" will convert `"www.google.com"` to
    `"\3www\6google\3com"`.
 */
unsigned char* label_encode(uint8_t* encoded, size_t n, uint8_t const* host);

/* Do the label decoding. Apart from the RLE decoding of
   `3www6google3com0` -> `www.google.com`, it also has a
   "(de)compression" scheme in which a label can be shared with
   another in the same buffer.
*/
int label_decode(uint8_t*       decoded,
                 size_t         n,
                 uint8_t const* src,
                 uint8_t const* buffer,
                 size_t         buffer_size,
                 size_t*        o_bytes_to_skip);


#endif /* defined INC_ENCODE_DECODE_LABEL */
