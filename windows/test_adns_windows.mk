SOURCEFILES = ..\lib\sockets\pbpal_adns_sockets.c ..\lib\pubnub_dns_codec.c ..\core\pubnub_assert_std.c

LDLIBS=ws2_32.lib IPHlpAPI.lib rpcrt4.lib

DEFINES=-D PUBNUB_THREADSAFE -D PUBNUB_LOG_LEVEL=PUBNUB_LOG_LEVEL_TRACE -D HAVE_STRERROR_S
CFLAGS = -Zi -MP -W3 $(DEFINES)
# -Zi enables debugging, remove to get a smaller .exe and no .pdb
# -MP use one compiler process for each input, faster on multi-core (ignored by clang-cl)
# -analyze To run the static analyzer (not compatible w/clang-cl)

INCLUDES=-I .. -I . -I ..\core\c99

all: pbpal_adns_sockets.exe

pbpal_adns_sockets.exe: $(SOURCEFILES)
	$(CC) $(CFLAGS) -DPUBNUB_CALLBACK_API -DPUBNUB_USE_ADNS=1 $(INCLUDES) $(SOURCEFILES) $(LDLIBS)

clean:
	del *.exe
	del *.obj
	del *.pdb
	del *.il?
	del *.lib
	del *.c~
	del *.h~
	del *~
