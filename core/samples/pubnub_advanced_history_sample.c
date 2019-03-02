/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_sync.h"

#include "core/pubnub_advanced_history.c"
#include "core/pubnub_helper.h"
#include "core/pubnub_timers.h"
//#include "core/pubnub_generate_uuid.h"

#include <stdio.h>
#include <time.h>


char* make_rand_name(char const* s)
{
    unsigned grn  = rand();
    char*    rslt = malloc(PUBNUB_MAX_CHANNEL_NAME_LENGTH + 1);
    if (NULL == rslt) {
        return rslt;
    }
    snprintf(rslt, PUBNUB_MAX_CHANNEL_NAME_LENGTH + 1, "%s_%X", s, grn);

    return rslt;
}


static void generate_uuid(pubnub_t* pbp)
{
    char const*                      uuid_default = "zeka-peka-iz-jendeka";
    struct Pubnub_UUID               uuid;
    static struct Pubnub_UUID_String str_uuid;

    if (0 != pubnub_generate_uuid_v4_random(&uuid)) {
        pubnub_set_uuid(pbp, uuid_default);
    }
    else {
        str_uuid = pubnub_uuid_to_string(&uuid);
        pubnub_set_uuid(pbp, str_uuid.uuid);
        printf("Generated UUID: %s\n", str_uuid.uuid);
    }
}


static void wait_useconds(unsigned long time_in_microseconds)
{
    clock_t  start = clock();
    unsigned long time_passed_in_microseconds;
    do {
        time_passed_in_microseconds = clock() - start;
    } while (time_passed_in_microseconds < time_in_microseconds);
}


static void sync_sample_free(pubnub_t* p)
{
    if (PN_CANCEL_STARTED == pubnub_cancel(p)) {
        enum pubnub_res pnru = pubnub_await(p);
        if (pnru != PNR_OK) {
            printf("Awaiting cancel failed: %d('%s')\n",
                   pnru,
                   pubnub_res_2_string(pnru));
        }
    }
    if (pubnub_free(p) != 0) {
        printf("Failed to free the Pubnub context\n");
    }
}


static int get_timetoken(pubnub_t* pbp, char* timetoken)
{
    enum pubnub_res res;

    puts("-----------------------");
    puts("Getting time...");
    puts("-----------------------");
    res = pubnub_time(pbp);
    if (res == PNR_STARTED) {
        res = pubnub_await(pbp);
    }
    if (PNR_OK == res) {
        strcpy(timetoken, pubnub_get(pbp));
        printf("Gotten time: '%s'\n", timetoken);
    }
    else {
        printf("Getting time failed with code %d('%s')\n",
               res,
               pubnub_res_2_string(res));
        return -1;
    }

    return 0;
}


int main(int argc, char* argv[])
{
    time_t          t0;
    char*           channel[5];
    int             msg_counts[sizeof channel/sizeof channel[0]];
    char            string_channels[500];
    int             timetoken_index[sizeof channel/sizeof channel[0]];
    char            timetokens[sizeof channel/sizeof channel[0]][30];
    char            string_timetokens[150];
    int msg_sent[sizeof channel/sizeof channel[0]][sizeof channel/sizeof channel[0]] = {{0},};
    enum pubnub_res res;
    char const*     pubkey = (argc > 1) ? argv[1] : "demo";
    char const*     keysub = (argc > 2) ? argv[2] : "demo";
    char const*     origin = (argc > 3) ? argv[3] : "pubsub.pubnub.com";
    pubnub_t*       pbp   = pubnub_alloc();
    pubnub_t*       pbp_2 = pubnub_alloc();
    int             n     = sizeof channel/sizeof channel[0];
    int             i;
    
    if ((NULL == pbp) || (NULL == pbp_2)) {
        printf("Failed to allocate Pubnub context!\n");
        return -1;
    }

    channel[0] = make_rand_name("pool");
    channel[1] = make_rand_name("lucky");
    channel[2] = make_rand_name("wild");
    channel[3] = make_rand_name("fast");
    channel[4] = make_rand_name("shot");
    
    pubnub_init(pbp, pubkey, keysub);
    pubnub_init(pbp_2, pubkey, keysub);
    generate_uuid(pbp);
    generate_uuid(pbp_2);
    pubnub_origin_set(pbp, origin);
    pubnub_origin_set(pbp_2, origin);

    for (i = 0; i < n; i++) {
        int j;
        
        while (get_timetoken(pbp, timetokens[i]) != 0) {
            /* wait in microseconds */
            wait_useconds(1000);
        }
        for (j = i; j < n; j++) {
            puts("-----------------------");
            puts("Publishing...");
            puts("-----------------------");
            res = pubnub_publish(
                pbp, channel[j], "\"Hello world from message_counts callback sample!\"");
            if (res == PNR_STARTED) {
                puts("Await publish");
                res = pubnub_await(pbp);
            }
            if (PNR_OK == res) {
                printf("Published! Response from Pubnub: %s\n",
                       pubnub_last_publish_result(pbp));
                ++msg_sent[i][j];
            }
            else if (PNR_PUBLISH_FAILED == res) {
                printf("Published failed on Pubnub, description: %s\n",
                       pubnub_last_publish_result(pbp));
            }
            else {
                printf("Publishing failed with code: %d('%s')\n", res, pubnub_res_2_string(res));
            }
        }
    }
    puts("--------------------------------------------message counts table-----------------------------------------");
    printf("               \\channels: %s |%s | %s | %s | %s |\n",
           channel[0],
           channel[1],
           channel[2],
           channel[3],
           channel[4]);
    for (i = 0 ; i < n; i++) {
        printf("tt[%d]'%s':       %d       |       %d       |       %d       |       %d       |       %d       |\n",
               i + 1,
               timetokens[i],
               msg_sent[i][0],
               msg_sent[i][1],
               msg_sent[i][2],
               msg_sent[i][3],
               msg_sent[i][4]);        
    }
    sprintf(string_channels,
            "%s,%s,%s,%s,%s",
           channel[0],
           channel[1],
           channel[2],
           channel[3],
           channel[4]);
    // Use current time as seed for random generator 
    srand(time(0)); 
    for (i = 0; i < 2; i++) {
        int  internal_msg_counts[sizeof channel/sizeof channel[0]] = {0};
        int j;

        if (0 == i) {
            int start_index =  rand() % (sizeof channel/sizeof channel[0]);
            timetoken_index[0] = start_index + 1;
            /* Internal message count used to compare against information obtained from response */
            for (j = start_index; j < n; j++) {
                int k;
                for (k = start_index; k <= j; k++) {
                    internal_msg_counts[j] += msg_sent[k][j];
                }
            }
        }
        else {
            for (j = 0; j < n; j++) {
                int k;
                int start_index =  rand() % (sizeof channel/sizeof channel[0]);
                timetoken_index[j] = start_index + 1;
                for (k = start_index; k <= j; k++) {
                    internal_msg_counts[j] += msg_sent[k][j];
                }
                // Use current time as seed for random generator 
                srand(time(0) + n - j); 
            }
        }
        time(&t0);
        if (i == 0) {
            puts("------------------------------------------------");
            puts("Getting message counts for a single timetoken...");
            puts("------------------------------------------------");
            res = pubnub_message_counts(pbp_2,
                                        string_channels,
                                        timetokens[timetoken_index[0] - 1],
                                        NULL);
        }
        else {
            sprintf(string_timetokens,
                    "%s,%s,%s,%s,%s",
                    timetokens[timetoken_index[0] - 1],
                    timetokens[timetoken_index[1] - 1],
                    timetokens[timetoken_index[2] - 1],
                    timetokens[timetoken_index[3] - 1],
                    timetokens[timetoken_index[4] - 1]);
            puts("----------------------------------------------------");
            puts("Getting message counts for the list of timetokens...");
            puts("----------------------------------------------------");
            res = pubnub_message_counts(pbp_2,
                                        string_channels,
                                        NULL,
                                        string_timetokens);
        }
        if (res == PNR_STARTED) {
            res = pubnub_await(pbp_2);
        }
        printf("Getting message counts lasted %lf seconds.\n", difftime(time(NULL), t0));
        if (PNR_OK == res) {
            int j;
            if (pubnub_get_chan_msg_counts_size(pbp_2) == sizeof channel/sizeof channel[0]) {
                puts("-----------------------------------Got message counts for all channels!----------------------------------");
            }
            pubnub_get_message_counts(pbp_2, string_channels, msg_counts);
            if (0 == i) {
                printf("tt[%d]='%s':", timetoken_index[0], timetokens[timetoken_index[0] - 1]);
            }
            else {
                printf("tt[%d]='%s'|tt[%d]='%s'|tt[%d]='%s'|tt[%d]='%s'|tt[%d]='%s'|\n",
                       timetoken_index[0],
                       timetokens[timetoken_index[0] - 1],
                       timetoken_index[1],
                       timetokens[timetoken_index[1] - 1],
                       timetoken_index[2],
                       timetokens[timetoken_index[2] - 1],
                       timetoken_index[3],
                       timetokens[timetoken_index[3] - 1],
                       timetoken_index[4],
                       timetokens[timetoken_index[4] - 1]);
            }
            for (j = 0 ; j < n; j++) {
                if ((msg_counts[j] > 0) && (msg_counts[j] != internal_msg_counts[j])) {
                    printf("Message counter mismatch! - "
                           "msg_counts[%d]=%d, "
                           "internal_msg_counts[%d]=%d |",
                           j,
                           msg_counts[j],
                           j,
                           internal_msg_counts[j]);
                }
                else {
                    printf("%s   %d   %s|",
                           (0 == i) ? "" : "         ",
                           msg_counts[j],
                           (0 == i) ? "" : "         ");
                }
            }
            putchar('\n');
        }
        else {
            printf("Getting message counts failed with code: %d('%s')\n",
                   res,
                   pubnub_res_2_string(res));
        }
    }
    
           
    for (i = 0 ; i < n; i++) {
        free(channel[i]);
    }    
    sync_sample_free(pbp_2);
    sync_sample_free(pbp);

    puts("Pubnub message_counts demo over.");

    return 0;
}
