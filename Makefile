#CC = i686-w64-mingw32-gcc
#CPP = i686-w64-mingw32-g++
CC = x86_64-w64-mingw32-gcc
CPP = x86_64-w64-mingw32-g++
STRIP = strip

CFLAGS = -O2 -D_UNICODE

SOURCES =

all: test_notif.exe

test_notif.exe: test_notif.o $(SOURCES)
	$(CC) $(CFLAGS) -g -static -mconsole -mwindows -municode -o $@ $^
	$(STRIP) $@

clean:
	rm -f test_notif.exe *.o

%.o: %.cpp
	$(CPP) -s -flto -Wall -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -s -flto -c -o $@ $<
