#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdio.h>

int run_command(const char *const args[], char *output, size_t capacity);
void utc_now(char output[32]);
void csv_field(FILE *file, const char *value);

#endif
