CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -pedantic
INCLUDES := -DCONFIG_1300D_110 -Istubs -I../../../integrations/magiclantern/1300D.110/src -I../../experimental/yuv_anamorphic
KERNEL := ../../experimental/yuv_anamorphic/yuv_anamorphic.c

.PHONY: test asan
test:
	@set -eu; test_dir=$$(mktemp -d /tmp/t6-cpu-bench-test.XXXXXX); \
	trap 'rm -f "$$test_dir/test"; rmdir "$$test_dir"' EXIT; \
	$(CC) $(CFLAGS) $(INCLUDES) test.c $(KERNEL) -o "$$test_dir/test"; \
	"$$test_dir/test"

asan:
	@set -eu; test_dir=$$(mktemp -d /tmp/t6-cpu-bench-asan.XXXXXX); \
	trap 'rm -f "$$test_dir/test"; rmdir "$$test_dir"' EXIT; \
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	$(INCLUDES) test.c $(KERNEL) -o "$$test_dir/test"; \
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$$test_dir/test"
