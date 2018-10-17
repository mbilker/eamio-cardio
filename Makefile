CC = i686-w64-mingw32-gcc
CPP = i686-w64-mingw32-g++

CC_64 = x86_64-w64-mingw32-gcc
CPP_64 = x86_64-w64-mingw32-g++

STRIP = strip

#CFLAGS = -O2 -D_UNICODE -DDBT_DEBUG -DEAMIO_DEBUG
CFLAGS = -O2 -D_UNICODE -DDBT_DEBUG
#CFLAGS = -O2 -D_UNICODE
LDFLAGS = -lsetupapi -lhid -lole32

SOURCES = hid.o log.o
SOURCES_32 = $(foreach source,$(SOURCES),build/32/$(source))
SOURCES_64 = $(foreach source,$(SOURCES),build/64/$(source))

GIT_REVISION = $(shell git rev-list --count HEAD)
GIT_COMMIT = $(shell git rev-parse --short HEAD)

all: build build/test_hid.exe build/32/eamio.dll build/64/eamio.dll

build:
	mkdir build build/32 build/64

build/test_hid.exe: build/32/test_hid.o build/32/window.o $(SOURCES_32)
	$(CC) $(CFLAGS) -g -static -mconsole -mwindows -municode -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

build/32/eamio.dll: build/32/eamio.o $(SOURCES_32)
	$(CC) $(CFLAGS) -shared -flto -municode -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

build/64/eamio.dll: build/64/eamio.o $(SOURCES_64)
	$(CC_64) $(CFLAGS) -shared -flto -municode -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f build/test_hid.exe build/32/* build/64/*
	rmdir build/64 build/32 build

build/32/eamio.o: eamio.c
	$(CC) $(CFLAGS) -Wno-format-security -s -flto \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<

build/64/eamio.o: eamio.c
	$(CC_64) $(CFLAGS) -Wno-format-security -s -flto \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<

build/32/%.o: %.c
	$(CC) $(CFLAGS) -s -flto -municode \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<

build/32/%.o: %.cpp
	$(CPP) $(CFLAGS) -s -flto -municode \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<

build/64/%.o: %.c
	$(CC_64) $(CFLAGS) -s -flto -municode \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<

build/64/%.o: %.cpp
	$(CPP_64) $(CFLAGS) -s -flto -municode \
		-DGIT_REVISION=\"$(GIT_REVISION)\" \
		-DGIT_COMMIT=\"$(GIT_COMMIT)\" \
		-c -o $@ $<
