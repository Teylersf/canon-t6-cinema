CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -pedantic
ENGINE := ../../engine
SOURCES := frame_source.c $(ENGINE)/se.c $(ENGINE)/cr2.c $(ENGINE)/jpeg_dec.c $(ENGINE)/anamorphic.c
HEADERS := $(wildcard $(ENGINE)/*.h)

.PHONY: all test asan
all: build/hevc-frame-source

build/hevc-frame-source: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -I$(ENGINE) $(SOURCES) -o $@

test: build/hevc-frame-source
	python3 test_frame_source.py ./build/hevc-frame-source ../../samples/T6_CC0.CR2

asan:
	$(MAKE) build/hevc-frame-source-asan
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 test_frame_source.py ./build/hevc-frame-source-asan ../../samples/T6_CC0.CR2

build/hevc-frame-source-asan: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -I$(ENGINE) $(SOURCES) -o $@
