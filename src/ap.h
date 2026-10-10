#ifndef AP_H
#define AP_H

#include <stddef.h>

int scanner_prepare(const char *interface, int *restore_managed, char *error, size_t length);
void scanner_restore(const char *interface, int restore_managed);
int ap_start(const char *interface, const char *ssid, const char *password,
             const char *profile, char *ip, size_t ip_length, int *active,
             char *error, size_t error_length);
void ap_stop(const char *profile);

#endif
