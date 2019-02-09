/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if PUBNUB_USE_IPV6
#include "core/pubnub_assert.h"
#include "core/pubnub_dns_servers.h"

#include "core/pubnub_assert.h"
#include "core/pubnub_log.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

int pubnub_parse_ipv6_addr(char const* addr, struct pubnub_ipv6_address* p)
{
    uint16_t b[8] = {0};
    /* Numerical value between two colons */
    uint16_t value = 0;
    /* Number of hex digits in numerical value */
    uint8_t hex_digits = 0;
    /* Number of colons found in address string */
    uint8_t colons = 0;
    /* Index of hex number(between two colons) in the array of eight 'unsigned short' members */
    uint8_t i = 0;
    /* power of number 16 */
    unsigned power_of_16;
    /* If turned to false address string is read from its end toward beginning(from right_to_left) */
    bool read_left_to_right = true;
    /* Indicates if previously read character was colon */
    bool previous_colon = false;
    char const* pos = addr;
    /* Position in the address string where analisis stops */
    char const* meeting_position;
    
    PUBNUB_ASSERT_OPT(addr != NULL);
    PUBNUB_ASSERT_OPT(p != NULL);

    if (strchr(addr, ':') == NULL) {
        PUBNUB_LOG_ERROR("Error: pubnub_parse_ipv6_addr('%s') - "
                         "No colons in the Ipv6 address string\n",
                         addr);
        return -1;
    }
    meeting_position = addr + strlen(addr);
    while (pos != meeting_position) {
        if (':' == *pos) {
            ++colons;
            if (colons > 7) {
                PUBNUB_LOG_ERROR("pubnub_parse_ipv6_addr('%s') - "
                                 "More than 7 colons in the Ipv6 address string\n",
                                 addr);
                return -1;
            }
            /* Saving the value between two colons */
            b[i] = value;
            /* Checks if this is the second colon in a row */ 
            if (previous_colon) {
                if (read_left_to_right) {
                    /* Will start reading from the other end of the address string
                       from right to left
                     */
                    read_left_to_right = false;
                    meeting_position = pos;
                    pos = addr + strlen(addr) - 1;
                    i = 7;
                }
                else {
                    PUBNUB_LOG_ERROR("Error :pubnub_parse_ipv6_addr('%s') - "
                                     "Can't have more than one close pair of colons"
                                     " in the Ipv6 address string\n",
                                     addr);
                    return -1;
                }
            }
            else {
                if (read_left_to_right) {
                    i++;
                    pos++;
                }
                else {
                    i--;
                    pos--;
                }
            }
            hex_digits = 0;
            power_of_16 = 1;
            value = 0;
            previous_colon = true;
        }
        else if (isdigit(*pos)) {
            ++hex_digits;
            if (hex_digits > 4) {
                PUBNUB_LOG_ERROR("Error :pubnub_parse_ipv6_addr('%s') - "
                                 "More than 4 hex digits together in the Ipv6 address string\n",
                                 addr);
                return -1;
            }
            previous_colon = false;
            if (read_left_to_right) {
                /* forming the value while reading from left to right */ 
                value = value*16 + *(pos++) - '0';
            }
            else {
                /* forming the value while reading from right to left */ 
                value = (*(pos--) - '0')*power_of_16 + value;
                power_of_16 *= 16;
            }
        }
        else if ((('a' <= *pos) && (*pos <= 'f'))
                 || (('A' <= *pos) && (*pos <= 'F'))) {
            ++hex_digits;
            if (hex_digits > 4) {
                PUBNUB_LOG_ERROR("Error :pubnub_parse_ipv6_addr('%s') - "
                                 "More than 4 hex digits together in the Ipv6 address string\n",
                                 addr);
                return -1;
            }
            previous_colon = false;
            if (read_left_to_right) {
                /* forming the value while reading from left to right */ 
                value = value*16 + toupper(*(pos++)) - 'A' + 10;
            }
            else {
                /* forming the value while reading from right to left */ 
                value = (toupper(*(pos--)) - 'A' + 10)*power_of_16 + value;
                power_of_16 *= 16;
            }
        }
        else {
            PUBNUB_LOG_ERROR("Error :pubnub_parse_ipv6_addr('%s') - "
                             "Invalid charactes in the Ipv6 address string\n",
                             addr);
            return -1;
        }
    }
    /* Address is stored in network order(big endian) */
    for(i = 0; i < 8; i++) {
        p->ipv6[2*i] = b[i] >> 8;
        p->ipv6[2*i+1] = b[i] & 0xFF;
    }
    
    return 0;
}
#endif /* PUBNUB_USE_IPV6 */
