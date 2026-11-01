#ifndef T6_HEVC_TEST_FIO_H
#define T6_HEVC_TEST_FIO_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define O_RDONLY 0u
FILE *FIO_OpenFile(const char *,unsigned);
int FIO_ReadFile(FILE *,void *,size_t);
int FIO_WriteFile(FILE *,const void *,size_t);
void FIO_CloseFile(FILE *);
int FIO_GetFileSize(const char *,uint32_t *);
int FIO_RenameFile(const char *,const char *);
int FIO_RemoveFile(const char *);
int is_file(const char *);
#endif
