#ifndef T6_CPU_TEST_MEM_H
#define T6_CPU_TEST_MEM_H
#include <stddef.h>
void *fio_malloc(size_t size);
void fio_free(void *pointer);
#endif
