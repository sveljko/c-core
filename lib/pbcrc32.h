#if !defined INC_PB_CRC32
#define	INC_PB_CRC32

#include <stdint.h>
#include <stdlib.h>

/* The standard CRC32 checksum(Ciclic Redundancy Checksum).
   Stores the checksum at @p crc address for (unpacked) @p data with given @p n_bytes length
   in octets.
 */ 
void pbcrc32(const void *data, size_t n_bytes, uint32_t* crc);

#endif /* INC_PB_CRC32 */

