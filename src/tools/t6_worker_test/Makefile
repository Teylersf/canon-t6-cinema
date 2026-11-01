CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -Wno-unused-function
ENGINE := $(wildcard ../../engine/*.c)
INCLUDES := -Istubs -I../../engine -I../../../integrations/magiclantern/1300D.110/src
DEFINES := -DSE_HOSTED -DCONFIG_1300D_110
OUT ?= /tmp/t6-worker-test
FIXTURE ?= ../../samples/T6_CC0.CR2
STOCK ?= ../../stocks/portra.fs

.PHONY: all test asan retro retro-asan
all:
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) test.c $(ENGINE) -lm -o $(OUT)
test: all
	$(OUT) $(FIXTURE) $(STOCK)
asan:
	$(MAKE) all CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" OUT=/tmp/t6-worker-test-asan
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/t6-worker-test-asan $(FIXTURE) $(STOCK)
retro:
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) retro_test.c ../../../integrations/magiclantern/1300D.110/src/film_retro_state.c ../../../integrations/magiclantern/1300D.110/src/film_retro_draw.c -o /tmp/t6-retro-test
	/tmp/t6-retro-test
retro-asan:
	$(MAKE) retro CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
