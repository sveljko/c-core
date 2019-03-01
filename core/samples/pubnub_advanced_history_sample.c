/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_callback.h"

#include "core/pubnub_advanced_history.c"
#include "core/pubnub_helper.h"
#include "core/pubnub_timers.h"
#include "core/pubnub_generate_uuid.h"
#include "core/pubnub_free_with_timeout.h"

#if defined _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdio.h>
#include <time.h>


/** Data that we pass to the Pubnub context and will get back via
    callback. To signal reception of response from Pubnub, that we get
    via callback, we use a condition variable w/pthreads and an Event
    on Windows.
*/
struct UserData {
#if defined _WIN32
    CRITICAL_SECTION mutw;
    HANDLE           condw;
#else
    pthread_mutex_t mutw;
    bool            triggered;
    pthread_cond_t  condw;
#endif
    pubnub_t* pb;
};


void sample_callback(pubnub_t*         pb,
                     enum pubnub_trans trans,
                     enum pubnub_res   result,
                     void*             user_data)
{
    struct UserData* pUserData = (struct UserData*)user_data;

    switch (trans) {
    case PBTT_PUBLISH:
        printf("Published, result: %d('%s')\n", result, pubnub_res_2_string(result));
        break;
    case PBTT_SUBSCRIBE:
        printf("Subscribed, result: %d('%s')\n", result, pubnub_res_2_string(result));
        break;
    case PBTT_TIME:
        printf("Timed, result: %d('%s')\n", result, pubnub_res_2_string(result));
        break;
    case PBTT_HISTORY:
        printf("Historied, result: %d('%s')\n", result, pubnub_res_2_string(result));
        break;
    case PBTT_MESSAGE_COUNTS:
        printf("'Advanced history' message_counts, result: %d('%s')\n",
               result,
               pubnub_res_2_string(result));
        break;
    default:
        printf("None?! result: %d('%s')\n", result, pubnub_res_2_string(result));
        break;
    }
#if defined _WIN32
    SetEvent(pUserData->condw);
#else
    pthread_mutex_lock(&pUserData->mutw);
    pUserData->triggered = true;
    pthread_cond_signal(&pUserData->condw);
    pthread_mutex_unlock(&pUserData->mutw);
#endif
}


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


static enum pubnub_res await(struct UserData* pUserData)
{
#if defined _WIN32
    ResetEvent(pUserData->condw);
    WaitForSingleObject(pUserData->condw, INFINITE);
#else
    pthread_mutex_lock(&pUserData->mutw);
    pUserData->triggered = false;
    while (!pUserData->triggered) {
        pthread_cond_wait(&pUserData->condw, &pUserData->mutw);
    }
    pthread_mutex_unlock(&pUserData->mutw);
#endif
    return pubnub_last_result(pUserData->pb);
}


static void InitUserData(struct UserData* pUserData, pubnub_t* pb)
{
#if defined _WIN32
    InitializeCriticalSection(&pUserData->mutw);
    pUserData->condw = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    pthread_mutex_init(&pUserData->mutw, NULL);
    pthread_cond_init(&pUserData->condw, NULL);
#endif
    pUserData->pb = pb;
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


static void wait_seconds(double time_in_seconds)
{
    time_t  start = time(NULL);
    double time_passed_in_seconds;
    do {
        time_passed_in_seconds = difftime(time(NULL), start);
    } while (time_passed_in_seconds < time_in_seconds);
}


static void callback_sample_free(pubnub_t* p)
{
    if (pubnub_free_with_timeout(p, 1000) != 0) {
        printf("Failed to free the Pubnub context\n");
    }
    else {
        /* Waits for the context to be released from the processing queue */
        wait_seconds(1);
    }
}


static int get_timetoken(pubnub_t* pbp, struct UserData* pUserData, char* timetoken)
{
    enum pubnub_res res;

    puts("-----------------------");
    puts("Getting time...");
    puts("-----------------------");
    res = pubnub_time(pbp);
    if (res != PNR_STARTED) {
        printf("pubnub_time() returned unexpected %d('%s')\n",
               res,
               pubnub_res_2_string(res));
        return -1;
    }
    res = await(pUserData);
    if (res == PNR_STARTED) {
        printf("await() returned unexpected: PNR_STARTED(%d)\n", res);
        return -1;
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
    struct UserData user_data;
    struct UserData user_data_2;
    char const*     pubkey = (argc > 1) ? argv[1] : "demo";
    char const*     keysub = (argc > 2) ? argv[2] : "demo";
    char const*     origin = (argc > 3) ? argv[3] : "pubsub.pubnub.com";
    pubnub_t*       pbp   = pubnub_alloc();
    pubnub_t*       pbp_2 = pubnub_alloc();
    int             n     = sizeof channel/sizeof channel[0];
    int             i;
    
    if (NULL == pbp) {
        printf("Failed to allocate Pubnub context!\n");
        return -1;
    }
    if (NULL == pbp_2) {
        printf("Failed to allocate Pubnub context!\n");
        return -1;
    }

    channel[0] = make_rand_name("brza_fotografija");
    channel[1] = make_rand_name("ljubazni_vodoinstalater");
    channel[2] = make_rand_name("zustri_steva");
    channel[3] = make_rand_name("zemljanin");
    channel[4] = make_rand_name("sima_kosmos");
    
    
    InitUserData(&user_data, pbp);
    InitUserData(&user_data_2, pbp_2);

    pubnub_init(pbp, pubkey, keysub);
    pubnub_register_callback(pbp, sample_callback, &user_data);
    pubnub_init(pbp_2, pubkey, keysub);
    pubnub_register_callback(pbp_2, sample_callback, &user_data_2);
    generate_uuid(pbp);
    generate_uuid(pbp_2);
    pubnub_origin_set(pbp, origin);
    pubnub_origin_set(pbp_2, origin);

    for (i = 0; i < n; i++) {
        int j;
        
        while (get_timetoken(pbp, &user_data, timetokens[i]) != 0) {
            /* wait in microseconds */
            wait_useconds(5000);
        }
        for (j = i; j < n; j++) {
            puts("-----------------------");
            puts("Publishing...");
            puts("-----------------------");
            res = pubnub_publish(
                pbp, channel[j], "\"Hello world from message_counts callback sample!\"");
            if (res != PNR_STARTED) {
                printf("pubnub_publish() returned unexpected: %d('%s')\n",
                       res,
                       pubnub_res_2_string(res));
                for (i = 0 ; i < sizeof channel/sizeof channel[0]; i++) {
                    free(channel[i]);
                }    
                callback_sample_free(pbp);
                callback_sample_free(pbp_2);
                return -1;
            }
            puts("Await publish");
            res = await(&user_data);
            if (res == PNR_STARTED) {
                printf("await() returned unexpected: PNR_STARTED(%d)\n", res);
                for (i = 0 ; i < sizeof channel/sizeof channel[0]; i++) {
                    free(channel[i]);
                }    
                callback_sample_free(pbp);
                callback_sample_free(pbp_2);
                return -1;
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
        /* wait in microseconds */
        wait_useconds(1000);
    }
    puts("-----------------------------------------message counts table---------------------------------------");
    printf("\\channels: %s | %s | %s | %s | %s |\n",
           channel[0],
           channel[1],
           channel[2],
           channel[3],
           channel[4]);
    for (i = 0 ; i < n; i++) {
        printf("tt[%d]'%s':      %d       |       %d       |      %d       |      %d       |      %d      |\n",
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
    for (;;) {
        int  internal_msg_counts[sizeof channel/sizeof channel[0]] = {0};
        char c;
        puts("Enter ordinal number of a single time token, or 'space' separated list of "
             "timetokens ordinal numbers for the given group of channels,");
        puts("or just press 'enter' to quit.");
        for (i = 0, c = getchar(); (c != '\n') && (i < n); c = getchar()) {
            ungetc(c, stdin);
            scanf("%d", &(timetoken_index[i]));
            if (timetoken_index[i] > n) {
                puts("Timetoken ordinal index is grater than permited and will be ignored.");
            }
            else {
                int j;
                /* Internal message count used to compare against information obtained from response */
                for (j = timetoken_index[i] - 1; j < n; j++) {
                    internal_msg_counts[i] += msg_sent[j][i];
                }
                i++;
            }
        }
        time(&t0);
        if (i == 1) {
            /* Internal message count used to compare against information obtained from response */
            for (; i < n; i++) {
                int j;
                for (j = timetoken_index[0] - 1; j < n; j++) {
                    internal_msg_counts[i] += msg_sent[j][i];
                }
            }
            puts("------------------------------------------------");
            puts("Getting message counts for a single timetoken...");
            puts("------------------------------------------------");
            res = pubnub_message_counts(pbp_2,
                                        string_channels,
                                        timetokens[timetoken_index[0] - 1],
                                        NULL);
        }
        else if (i == n) {
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
        else if (i == 0) {
            break;
        }
        else {
            puts("If there is more than one timetoken index, "
                 "than they have to match number of channels.");
            continue;
        }
        if (res != PNR_STARTED) {
             printf("pubnub_message_counts() returned unexpected: %d('%s')\n",
                    res,
                    pubnub_res_2_string(res));
             for (i = 0 ; i < sizeof channel/sizeof channel[0]; i++) {
                 free(channel[i]);
             }    
             callback_sample_free(pbp);
             callback_sample_free(pbp_2);
             return -1;
        }
        res = await(&user_data_2);
        printf("Getting message counts lasted %lf seconds.\n", difftime(time(NULL), t0));
        if (res == PNR_STARTED) {
             printf("await() returned unexpected: PNR_STARTED(%d)\n", res);
             for (i = 0 ; i < sizeof channel/sizeof channel[0]; i++) {
                 free(channel[i]);
             }    
             callback_sample_free(pbp);
             callback_sample_free(pbp_2);
             return -1;
        }
        else if (PNR_OK == res) {
            if (pubnub_get_chan_msg_counts_size(pbp_2) == sizeof channel/sizeof channel[0]) {
                puts("-----------------------------------Got message counts for all channels!----------------------------------");
            }
            pubnub_get_message_counts(pbp_2, string_channels, msg_counts);
            for (i = 0 ; i < n; i++) {
                if ((msg_counts[i] > 0) && (msg_counts[i] != internal_msg_counts[i])) {
                    printf("Message counter mismatch! - "
                           "msg_counts[%d]=%d, "
                           "internal_msg_counts[%d]=%d |",
                           i,
                           msg_counts[i],
                           i,
                           internal_msg_counts[i]);
                }
                else {
                    printf("          %d         |", msg_counts[i]);
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
    callback_sample_free(pbp_2);
    callback_sample_free(pbp);

    puts("Pubnub message_counts callback demo over.");

    return 0;
}
