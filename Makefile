CC = i686-w64-mingw32-gcc
CPP = i686-w64-mingw32-g++
#CC = x86_64-w64-mingw32-gcc
#CPP = x86_64-w64-mingw32-g++
STRIP = strip

#CFLAGS = -O2 -D_UNICODE -DDBT_DEBUG -DEAMIO_DEBUG
CFLAGS = -O2 -D_UNICODE

SOURCES = drive_check.o window.o

all: test_notif.exe eamio.dll eamio_bt5.dll

test_notif.exe: test_notif.o $(SOURCES)
	$(CC) $(CFLAGS) -g -static -mconsole -mwindows -municode -o $@ $^
	$(STRIP) $@

eamio.dll: eamio.o $(SOURCES)
	$(CC) $(CFLAGS) -shared -flto -municode -o $@ $^
	$(STRIP) $@

eamio_bt5.dll: eamio.c $(SOURCES)
	$(CC) $(CFLAGS) -DWITH_ORIG_EAMIO -shared -flto -municode -Wno-format-security -o $@ $^
	$(STRIP) $@

clean:
	rm -f test_notif.exe eamio.dll *.o

%.o: %.cpp
	$(CPP) -s -flto -Wall -c -o $@ $<

eamio.o: eamio.c
	$(CC) $(CFLAGS) -Wno-format-security -s -flto -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -s -flto -c -o $@ $<
