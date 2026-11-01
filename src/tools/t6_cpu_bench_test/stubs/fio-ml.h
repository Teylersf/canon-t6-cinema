#ifndef T6_CPU_TEST_FIO_H
#define T6_CPU_TEST_FIO_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define O_RDONLY 0u
FILE *FIO_OpenFile(const char *path, unsigned mode);
int FIO_ReadFile(FILE *stream, void *dst, size_t count);
int FIO_WriteFile(FILE *stream, const void *src, size_t count);
void FIO_CloseFile(FILE *stream);
int FIO_GetFileSize(const char *path, uint32_t *size);
int FIO_RenameFile(const char *source, const char *destination);
int FIO_RemoveFile(const char *path);
int is_file(const char *path);
#endif
