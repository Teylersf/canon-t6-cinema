#ifndef T6_HEVC_TEST_MEM_H
#define T6_HEVC_TEST_MEM_H
#include <stddef.h>
void *fio_malloc(size_t size);
void fio_free(void *pointer);
#endif
