#ifndef FH2_SUPPORT_H
#define FH2_SUPPORT_H
#include "extra_options.h"
void memory_copy(void *destination, const void *source, size_t size);
int memory_equal(const void *left, const void *right, size_t size);
size_t text_length(const char *text);
int text_equal_ignore_case(const char *left, const char *right);
int memory_read(const void *address, void *output, size_t size);
int memory_is_writable(const void *address, size_t size);
void status_set(const char *message);
void status_append(const char *message);
const char *status_get(void);
int path_join(wchar_t output[PATH_CAPACITY], const wchar_t *directory, const wchar_t *name);
#endif
