SOURCEFILES = ../lib/sockets/pbpal_adns_sockets.c ../lib/pubnub_dns_codec.c ../core/pubnub_assert_std.c

ifndef USE_PROXY
USE_PROXY = 1
endif

OS := $(shell uname)
ifeq ($(OS),Darwin)
LDLIBS=-lpthread
else
LDLIBS=-lrt -lpthread
endif

CFLAGS =-g -Wall -D PUBNUB_THREADSAFE -D PUBNUB_LOG_LEVEL=PUBNUB_LOG_LEVEL_TRACE -D PUBNUB_PROXY_API=$(USE_PROXY)

INCLUDES=-I .. -I .

all: pbpal_adns_sockets

pbpal_adns_sockets: $(SOURCEFILES)
	$(CC) -o $@ -D PUBNUB_CALLBACK_API $(CFLAGS) $(INCLUDES) $(SOURCEFILES) $(LDLIBS)

clean:
	rm pbpal_adns_sockets *.o *.dSYM
