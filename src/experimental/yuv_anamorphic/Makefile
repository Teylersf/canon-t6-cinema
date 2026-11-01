CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -pedantic
ARM_CC ?= arm-none-eabi-gcc
ARM_SIZE ?= arm-none-eabi-size
ARM_NM ?= arm-none-eabi-nm

.PHONY: test asan arm
test:
	@set -eu; test_dir=$$(mktemp -d /tmp/t6-yuv-test.XXXXXX); \
	trap 'rm -f "$$test_dir/test"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) test_yuv_anamorphic.c yuv_anamorphic.c -o "$$test_dir/test"; \
	"$$test_dir/test"

asan:
	@set -eu; test_dir=$$(mktemp -d /tmp/t6-yuv-asan.XXXXXX); \
	trap 'rm -f "$$test_dir/test"; rmdir "$$test_dir"' EXIT; \
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	test_yuv_anamorphic.c yuv_anamorphic.c -o "$$test_dir/test"; \
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$$test_dir/test"

arm:
	@set -eu; test_dir=$$(mktemp -d /tmp/t6-yuv-arm.XXXXXX); \
	trap 'rm -f "$$test_dir/yuv_anamorphic.o"; rmdir "$$test_dir"' EXIT; \
	$(ARM_CC) -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
	-mcpu=arm946e-s -marm -ffreestanding -fno-builtin \
	-c yuv_anamorphic.c -o "$$test_dir/yuv_anamorphic.o"; \
	$(ARM_SIZE) -A "$$test_dir/yuv_anamorphic.o"; \
	$(ARM_NM) -u "$$test_dir/yuv_anamorphic.o"
