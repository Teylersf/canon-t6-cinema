CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic
OUT ?= /tmp/t6-hevc-intra-synthetic
RAW_OUT ?= /tmp/t6-hevc-intra-raw420
.PHONY: all raw arm test asan decode
all:
	$(CC) $(CFLAGS) synthetic.c hevc_intra.c -o "$(OUT)"
raw:
	$(CC) $(CFLAGS) raw420.c hevc_intra.c -o "$(RAW_OUT)"
arm:
	arm-none-eabi-gcc $(CFLAGS) -mcpu=arm946e-s -marm -ffreestanding -fno-builtin -c hevc_intra.c -o /tmp/t6-hevc-intra.o
	arm-none-eabi-size /tmp/t6-hevc-intra.o
	arm-none-eabi-nm -u /tmp/t6-hevc-intra.o
test:
	@set -eu; d=$$(mktemp -d /tmp/t6-hevc-test.XXXXXX); trap 'rm -f "$$d/test"; rmdir "$$d"' EXIT; \
	$(CC) $(CFLAGS) test.c hevc_intra.c -o "$$d/test"; "$$d/test"
asan:
	@set -eu; d=$$(mktemp -d /tmp/t6-hevc-asan.XXXXXX); trap 'rm -f "$$d/test"; rmdir "$$d"' EXIT; \
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	test.c hevc_intra.c -o "$$d/test"; \
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$$d/test"
decode:
	python3 verify_decode.py
