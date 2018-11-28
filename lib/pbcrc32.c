/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "lib/pbcrc32.h"

static uint32_t crc32_for_byte(uint32_t r)
{
    int j;
    for(j = 0; j < 8; ++j) {
        r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
    }
    return r ^ (uint32_t)0xFF000000L;
}

uint32_t pbcrc32(const void *data, size_t n_bytes)
{
    static uint32_t table[0x100];
    uint32_t crc = 0;
    size_t i;
    if(!*table) {
        for(i = 0; i < 0x100; ++i) {
            table[i] = crc32_for_byte(i);
        }
    }
    for(i = 0; i < n_bytes; ++i) {
        crc = table[(uint8_t)crc ^ ((uint8_t*)data)[i]] ^ crc >> 8;
    }
    return crc;
}
