CC = i686-w64-mingw32-gcc
CPP = i686-w64-mingw32-g++

CC_64 = x86_64-w64-mingw32-gcc
CPP_64 = x86_64-w64-mingw32-g++

STRIP = strip

CFLAGS = -O2 -D_UNICODE -Wall -DDBT_DEBUG -DEAMIO_DEBUG
#CFLAGS = -O2 -D_UNICODE -DDBT_DEBUG -DHID_DEBUG
#CFLAGS = -O2 -D_UNICODE
LDFLAGS = -lsetupapi -lhid -lole32

SOURCES = hid.o log.o window.o
SOURCES_32 = $(foreach source,$(SOURCES),build/32/$(source))
SOURCES_64 = $(foreach source,$(SOURCES),build/64/$(source))

GIT_REVISION = $(shell git rev-list --count HEAD)
GIT_COMMIT = $(shell git rev-parse --short HEAD)

all: build build/test_hid.exe build/32/eamio.dll build/64/eamio.dll

build:
	mkdir -p build/32 build/64

build/test_hid.exe: build build/32/test_hid.o $(SOURCES_32)
	$(CC) $(CFLAGS) -g -static -mconsole -mwindows -municode -o $@ $(filter-out $<,$^) $(LDFLAGS)

build/32/eamio.dll: build build/32/eamio.o $(SOURCES_32)
	$(CC) $(CFLAGS) -shared -flto -municode -o $@ $(filter-out $<,$^) $(LDFLAGS)
	$(STRIP) $@

build/64/eamio.dll: build build/64/eamio.o $(SOURCES_64)
	$(CC_64) $(CFLAGS) -shared -flto -municode -o $@ $(filter-out $<,$^) $(LDFLAGS)
	$(STRIP) $@

card-eamio-r$(GIT_REVISION)_$(GIT_COMMIT).zip: build/32/eamio.dll build/64/eamio.dll
	mkdir -p release/card-eamio/32 release/card-eamio/64
	cp build/32/eamio.dll release/card-eamio/32/eamio.dll
	cp build/64/eamio.dll release/card-eamio/64/eamio.dll
	rm -f $@
	(cd release; zip -r ../$@ card-eamio; cd ..)

card-eamio.zip: card-eamio-r$(GIT_REVISION)_$(GIT_COMMIT).zip
	ln -sf $< $@

clean:
	rm -f build/test_hid.exe build/32/* build/64/*
	rm -f release/card-eamio/32/* release/card-eamio/64/* *.zip
	rmdir \
		build/64 build/32 build \
		release/card-eamio/32 release/card-eamio/64 release/card-eamio release

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
