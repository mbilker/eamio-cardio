CC = i686-w64-mingw32-gcc
CPP = i686-w64-mingw32-g++
#CC = x86_64-w64-mingw32-gcc
#CPP = x86_64-w64-mingw32-g++
STRIP = strip

#CFLAGS = -O2 -D_UNICODE -DDBT_DEBUG -DEAMIO_DEBUG
CFLAGS = -O2 -D_UNICODE
LDFLAGS = -lsetupapi -lhid -lole32

SOURCES = hid.o log.o

all: test_hid.exe eamio.dll

test_hid.exe: test_hid.o $(SOURCES)
	$(CC) $(CFLAGS) -g -static -mconsole -mwindows -municode -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

eamio.dll: eamio.o $(SOURCES)
	$(CC) $(CFLAGS) -shared -flto -municode -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f test_notif.exe eamio.dll *.o

%.o: %.cpp
	$(CPP) -s -flto -Wall -c -o $@ $<

eamio.o: eamio.c
	$(CC) $(CFLAGS) -Wno-format-security -s -flto -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -s -flto -c -o $@ $<
