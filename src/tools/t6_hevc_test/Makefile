CC ?= cc
CFLAGS ?= -std=c99 -O2 -g -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic
INCLUDES := -DCONFIG_1300D_110 -Istubs -I../../../integrations/magiclantern/1300D.110/src -I../../experimental/hevc_intra
NATIVE := ../../../integrations/magiclantern/1300D.110/src/film_hevc.c
CORE := ../../experimental/hevc_intra/hevc_intra.c
.PHONY: test asan
test:
	@set -eu; d=$$(mktemp -d /tmp/t6-hevc-native-test.XXXXXX); \
	trap 'rm -f "$$d/native.o" "$$d/test" "$$d/native.HVC"; rmdir "$$d"' EXIT; \
	$(CC) $(CFLAGS) $(INCLUDES) -Dmalloc=test_malloc -Dfree=test_free -c $(NATIVE) -o "$$d/native.o"; \
	$(CC) $(CFLAGS) $(INCLUDES) test.c $(CORE) "$$d/native.o" -o "$$d/test"; \
	"$$d/test" "$$d/native.HVC"; \
	python3 verify_native.py "$$d/native.HVC"
asan:
	@set -eu; d=$$(mktemp -d /tmp/t6-hevc-native-asan.XXXXXX); \
	trap 'rm -f "$$d/native.o" "$$d/test" "$$d/native.HVC"; rmdir "$$d"' EXIT; \
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	$(INCLUDES) -Dmalloc=test_malloc -Dfree=test_free -c $(NATIVE) -o "$$d/native.o"; \
	$(CC) -std=c99 -O1 -g -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	$(INCLUDES) test.c $(CORE) "$$d/native.o" -o "$$d/test"; \
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$$d/test" "$$d/native.HVC"; \
	python3 verify_native.py "$$d/native.HVC"
