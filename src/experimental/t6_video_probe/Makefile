CC ?= cc
ARM_CC ?= arm-none-eabi-gcc
ARM_SIZE ?= arm-none-eabi-size
ARM_NM ?= arm-none-eabi-nm
STRICT = -std=c99 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
SOURCES = t6_video_probe.c test_t6_video_probe.c

.PHONY: test asan arm check

# Every output is in an isolated /tmp directory, removed even on failure.
test:
	@set -eu; d=$$(mktemp -d /tmp/t6-video-probe-test.XXXXXX); trap 'rm -rf "$$d"' EXIT HUP INT TERM; \
	$(CC) $(STRICT) -O2 $(SOURCES) -o "$$d/test"; "$$d/test"

asan:
	@set -eu; d=$$(mktemp -d /tmp/t6-video-probe-asan.XXXXXX); trap 'rm -rf "$$d"' EXIT HUP INT TERM; \
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(SOURCES) -o "$$d/test"; \
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$$d/test"

arm:
	@set -eu; d=$$(mktemp -d /tmp/t6-video-probe-arm.XXXXXX); trap 'rm -rf "$$d"' EXIT HUP INT TERM; \
	$(ARM_CC) $(STRICT) -Os -mcpu=arm946e-s -marm -ffreestanding -fno-builtin -fno-common -c t6_video_probe.c -o "$$d/probe.o"; \
	$(ARM_SIZE) "$$d/probe.o"; $(ARM_NM) -u "$$d/probe.o"

check: test asan arm
