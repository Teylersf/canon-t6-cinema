/* A deliberately restricted format contract, not a Canon formatter emulator.
 * The pinned ML snprintf wrapper calls the camera ROM's nonstandard vsnprintf
 * (1300D.110/stubs.S), so its implementation cannot run in a host C test.
 * QEMU demonstrated %.Ns is unsupported. Reject it before host libc can hide
 * the incompatibility. Restrict overlay calls to the subset it actually uses. */
#ifndef T6_TEST_NATIVE_FORMAT_H
#define T6_TEST_NATIVE_FORMAT_H
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
static int t6_native_format_allowed(const char *format)
{
    if (!format) return 0;
    while (*format) {
        int width = 0;
        if (*format++ != '%') continue;
        if (*format == '%') { format++; continue; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + *format++ - '0';
            if (width > 16) return 0;
        }
        if (*format == 's') { if (width) return 0; format++; continue; }
        if (*format == 'd' || *format == 'i' || *format == 'u' ||
            *format == 'x' || *format == 'X' || *format == 'c') { format++; continue; }
        return 0; /* no precision, *, length modifiers or floating point */
    }
    return 1;
}
static int t6_mock_snprintf(char *dst, size_t size, const char *format, ...)
{
    int result;
    va_list args;
    if (!t6_native_format_allowed(format)) {
        fprintf(stderr, "unsupported native format: %s\n", format ? format : "(null)");
        abort();
    }
    va_start(args, format); result = vsnprintf(dst, size, format, args); va_end(args);
    return result;
}
#endif
