#ifndef T6_TEST_FIO_H
#define T6_TEST_FIO_H
#include "dryos.h"
#define O_RDONLY 0
#define O_WRONLY 01
#define O_CREAT 0100
#define O_EXCL 0200
struct card_info { int file_number; };
struct card_info *get_shooting_card(void);
const char *get_dcim_dir(void);
int is_file(const char *path);
FILE *FIO_OpenFile(const char *path, unsigned mode);
FILE *FIO_CreateFile(const char *path);
int FIO_ReadFile(FILE *file, void *dst, size_t count);
int FIO_WriteFile(FILE *file, const void *src, size_t count);
void FIO_CloseFile(FILE *file);
int FIO_GetFileSize(const char *path, uint32_t *size);
int64_t FIO_SeekSkipFile(FILE *file, int64_t offset, int whence);
int FIO_RenameFile(const char *src, const char *dst);
int FIO_RemoveFile(const char *path);
size_t read_file(const char *path, void *dst, size_t size);
#endif
