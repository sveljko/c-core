/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub.hpp"

#include <iostream>

#include <ctime>
#include <cstdlib>

using namespace pubnub;

/** This example, while using the C++ API which is "generic" will
    surely work only with the Pubnub C sync "back-end" (notification
    interface). Whether it works with the "callback" interface depends
    on a particular implementation of that interface.
 */

int main()
{
    try {
        /* This is a widely used channel, something should happen there
           from time to time
        */
        std::string chan = "hello_world";
        pubnub::context pb("demo", "demo");
        time_t t0;
        std::string msg("\"Hello world from C++(published 'via post')!\"");
        std::string msg_for_gzip("\"Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')! - Hello world from C++(published 'via post with GZIP')!\"");

        /* This is essential, as otherwise waiting for incoming data will
           block! Since we're doing this, be sure to not enable verbose
           debugging, as you won't see a thing except endless lines of
           some tracing.
        */
        pb.set_blocking_io(pubnub::non_blocking);

        std::cout << "--------------------------" << std::endl
                  << "Publish via post..." << std::endl
                  << "--------------------------" << std::endl;

         time(&t0);
         /* parap 'msg' should be considered as dynamically allocated memory which can be freed only
            after the publish(via post) transaction is finished.
          */
         pubnub::futres futres1 = pb.publish(chan,
                                             msg,
                                             publish_options().method(pubnubPublishViaPOST));
         pubnub_res res = futres1.last_result();
         if (res == PNR_STARTED) {
             res = futres1.await();
         }
         std::cout << "Publish via post(C++) lasted " << difftime(time(NULL), t0)
                   << " seconds." << std::endl;
         if (PNR_OK == res) {
             std::cout << "Published! Response from Pubnub: " << pb.last_publish_result() << std::endl;
         }
         else if (PNR_PUBLISH_FAILED == res) {
             std::cout << "Published failed on Pubnub, description: "
                       << pb.last_publish_result() << std::endl;
         }
         else {
             std::cout << "Publishing failed with code: " << res << "('" << pubnub_res_2_string(res)
                       << "')" <<  std::endl;
         }

         std::cout << "--------------------------" << std::endl
                   << "Publish via post(with gzip)..." << std::endl
                   << "--------------------------" << std::endl;

         time(&t0);
         /* param 'msg' can be string literal, local string, or dynamically allocated memory.
            The message is packed into pubnub context buffer before the HTTP transaction begins.
          */
         pubnub::futres futres2 = pb.publish(chan,
                                             msg_for_gzip,
                                             publish_options().method(pubnubPublishViaPOSTwithGZIP));
         res = futres2.last_result();
         if (res == PNR_STARTED) {
             res = futres2.await();
         }
         std::cout << "Publish via post(C++) with gzip lasted " << difftime(time(NULL), t0)
                   << " seconds." << std::endl;
         if (PNR_OK == res) {
             std::cout << "Published! Response from Pubnub: " << pb.last_publish_result() << std::endl;
         }
         else if (PNR_PUBLISH_FAILED == res) {
             std::cout << "Published failed on Pubnub, description: "
                       << pb.last_publish_result() << std::endl;
         }
         else {
             std::cout << "Publishing failed with code: " << res << "('" << pubnub_res_2_string(res)
                       << "')" <<  std::endl;
         }
    }
    catch (std::exception &exc) {
        std::cout << "Caught exception: " << exc.what() << std::endl;
    }

std::cout << "Pubnub C++ " <<
#if defined(PUBNUB_CALLBACK_API)
    "callback " <<
#else
    "sync " <<
#endif
    "publish 'via post' demo over." << std::endl;

    return 0;
}
