#ifndef T6_CPU_TEST_MEM_H
#define T6_CPU_TEST_MEM_H
#include <stddef.h>
void *fio_malloc(size_t size);
void fio_free(void *pointer);
void *tmp_malloc(size_t size);
void tmp_free(void *pointer);
void *test_cacheable(void *pointer);
#define CACHEABLE(pointer) test_cacheable(pointer)
#endif
