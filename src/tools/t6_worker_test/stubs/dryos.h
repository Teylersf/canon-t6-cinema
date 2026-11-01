#ifndef T6_TEST_DRYOS_H
#define T6_TEST_DRYOS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int ml_started, ml_shutdown_requested;
unsigned get_ms_clock(void);
void msleep(int milliseconds);
#define TASK_CREATE(name, function, arg, priority, stack)
#endif
