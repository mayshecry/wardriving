#include "ap.h"
#include "util.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int scanner_prepare(const char *interface, int *restore_managed, char *error, size_t length)
{
    char output[2048];
    const char *const check[] = {"nmcli", "-g", "GENERAL.NM-MANAGED", "device", "show", interface, NULL};
    const char *const unmanage[] = {"nmcli", "device", "set", interface, "managed", "no", NULL};
    const char *const up[] = {"ip", "link", "set", "dev", interface, "up", NULL};

    *restore_managed = 0;
    if (run_command(check, output, sizeof(output)) != 0) {
        snprintf(error, length, "Cannot inspect scan adapter: %.180s", output);
        return -1;
    }
    if (strncmp(output, "yes", 3) == 0) {
        *restore_managed = 1;
        if (run_command(unmanage, output, sizeof(output)) != 0) {
            *restore_managed = 0;
            snprintf(error, length, "Cannot unmanage scan adapter: %.170s", output);
            return -1;
        }
    } else if (strncmp(output, "no", 2) != 0) {
        snprintf(error, length, "Unknown scan adapter state: %.170s", output);
        return -1;
    }
    if (run_command(up, output, sizeof(output)) != 0) {
        snprintf(error, length, "Cannot bring up scan adapter: %.170s", output);
        return -1;
    }
    return 0;
}

void scanner_restore(const char *interface, int restore_managed)
{
    char output[2048];
    if (restore_managed) {
        const char *const args[] = {"nmcli", "device", "set", interface, "managed", "yes", NULL};
        if (run_command(args, output, sizeof(output)) != 0)
            fprintf(stderr, "Could not restore scan adapter management: %s\n", output);
    }
}

int ap_start(const char *interface, const char *ssid, const char *password,
             const char *profile, char *ip, size_t ip_length, int *active,
             char *error, size_t error_length)
{
    char output[4096];
    struct in_addr address;
    const char *const hotspot[] = {"nmcli", "-w", "30", "device", "wifi", "hotspot",
                                    "ifname", interface, "con-name", profile,
                                    "ssid", ssid, "password", password, NULL};
    const char *const show[] = {"nmcli", "-g", "IP4.ADDRESS", "device", "show", interface, NULL};

    *active = 0;
    if (run_command(hotspot, output, sizeof(output)) != 0) {
        snprintf(error, error_length, "Could not start hotspot: %.180s", output);
        return -1;
    }
    *active = 1;
    if (run_command(show, output, sizeof(output)) != 0) {
        snprintf(error, error_length, "Could not read hotspot address: %.170s", output);
        return -1;
    }
    size_t size = strcspn(output, "/\r\n \t");
    if (size == 0 || size >= ip_length) {
        snprintf(error, error_length, "Hotspot has no usable IPv4 address");
        return -1;
    }
    memcpy(ip, output, size);
    ip[size] = '\0';
    if (inet_pton(AF_INET, ip, &address) != 1) {
        snprintf(error, error_length, "Hotspot returned invalid IPv4 address: %.48s", ip);
        return -1;
    }
    return 0;
}

void ap_stop(const char *profile)
{
    char output[4096];
    const char *const down[] = {"nmcli", "-w", "10", "connection", "down", profile, NULL};
    const char *const remove[] = {"nmcli", "connection", "delete", profile, NULL};
    if (run_command(down, output, sizeof(output)) != 0)
        fprintf(stderr, "Could not stop hotspot: %s\n", output);
    if (run_command(remove, output, sizeof(output)) != 0)
        fprintf(stderr, "Could not remove hotspot profile: %s\n", output);
}
