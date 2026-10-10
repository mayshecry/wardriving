#include "bluetooth.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int valid_address(const char *text)
{
    if (strlen(text) < 17)
        return 0;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (text[i] != ':')
                return 0;
        } else if (!isxdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return text[17] == ' ' || text[17] == '\r' || text[17] == '\0';
}

static void remove_ansi(char *text)
{
    char *read = text;
    char *write = text;
    while (*read) {
        if (read[0] == '\033' && read[1] == '[') {
            read += 2;
            while (*read && !(*read >= '@' && *read <= '~'))
                read++;
            if (*read)
                read++;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static void remember(BluetoothScanner *scanner, const char *address)
{
    for (size_t i = 0; i < scanner->seen_count; i++) {
        if (strcmp(scanner->seen[i], address) == 0)
            return;
    }
    if (scanner->seen_count == scanner->seen_capacity) {
        size_t next = scanner->seen_capacity ? scanner->seen_capacity * 2 : 128;
        void *grown = realloc(scanner->seen, next * sizeof(*scanner->seen));
        if (!grown)
            return;
        scanner->seen = grown;
        scanner->seen_capacity = next;
    }
    memcpy(scanner->seen[scanner->seen_count++], address, 18);
}

static void set_name(BluetoothDevice *device, const char *name)
{
    while (*name == ' ')
        name++;
    if (!*name)
        return;
    snprintf(device->name, sizeof(device->name), "%s", name);
    size_t end = strlen(device->name);
    while (end && (device->name[end - 1] == '\r' || device->name[end - 1] == ' '))
        device->name[--end] = '\0';
}

void bluetooth_scan_once(BluetoothScanner *scanner)
{
    const char *const command[] = {"bluetoothctl", "--timeout", "7", "scan", "on", NULL};
    char *output = malloc(512 * 1024);
    if (!output) {
        pthread_mutex_lock(&scanner->state->mutex);
        snprintf(scanner->state->snapshot.bluetooth_error,
                 sizeof(scanner->state->snapshot.bluetooth_error), "Not enough memory for Bluetooth scan");
        pthread_mutex_unlock(&scanner->state->mutex);
        return;
    }
    int code = run_command(command, output, 512 * 1024);
    remove_ansi(output);
    char timestamp[32];
    utc_now(timestamp);
    if (code != 0 || strstr(output, "No default controller available") ||
        strstr(output, "Failed to start discovery") || strstr(output, "org.bluez.Error")) {
        pthread_mutex_lock(&scanner->state->mutex);
        Snapshot *snapshot = &scanner->state->snapshot;
        snapshot->bluetooth_visible = 0;
        snapshot->bluetooth_shown = 0;
        snapshot->bluetooth_scans++;
        snprintf(snapshot->bluetooth_last_scan, sizeof(snapshot->bluetooth_last_scan), "%s", timestamp);
        snprintf(snapshot->bluetooth_error, sizeof(snapshot->bluetooth_error),
                 "Bluetooth discovery unavailable (code %d): %.170s", code, output);
        pthread_mutex_unlock(&scanner->state->mutex);
        free(output);
        return;
    }
    BluetoothDevice devices[MAX_BLUETOOTH] = {0};
    size_t shown = 0;
    char *saveptr;
    for (char *line = strtok_r(output, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        if (!strstr(line, "[NEW]") && !strstr(line, "[CHG]"))
            continue;
        char *address = strstr(line, "Device ");
        if (!address)
            continue;
        address += 7;
        if (!valid_address(address))
            continue;
        char mac[18];
        memcpy(mac, address, 17);
        mac[17] = '\0';
        for (int i = 0; i < 17; i++)
            mac[i] = (char)toupper((unsigned char)mac[i]);
        size_t index;
        for (index = 0; index < shown; index++) {
            if (strcmp(devices[index].address, mac) == 0)
                break;
        }
        if (index == shown) {
            if (shown >= MAX_BLUETOOTH)
                continue;
            snprintf(devices[shown].address, sizeof(devices[shown].address), "%s", mac);
            snprintf(devices[shown].name, sizeof(devices[shown].name), "Unknown device");
            shown++;
        }
        char *detail = address + 17;
        while (*detail == ' ')
            detail++;
        if (strstr(line, "[NEW]")) {
            set_name(&devices[index], detail);
        } else if (strncmp(detail, "RSSI:", 5) == 0) {
            char *end;
            long value = strtol(detail + 5, &end, 10);
            if (end != detail + 5 && value >= -127 && value <= 20) {
                devices[index].rssi = (int)value;
                devices[index].has_rssi = 1;
            }
        } else if (strncmp(detail, "Name:", 5) == 0) {
            set_name(&devices[index], detail + 5);
        } else if (strncmp(detail, "Alias:", 6) == 0 &&
                   strcmp(devices[index].name, "Unknown device") == 0) {
            set_name(&devices[index], detail + 6);
        }
    }
    double latitude, longitude;
    int has_fix;
    pthread_mutex_lock(&scanner->state->mutex);
    has_fix = scanner->state->snapshot.has_fix;
    latitude = scanner->state->snapshot.latitude;
    longitude = scanner->state->snapshot.longitude;
    pthread_mutex_unlock(&scanner->state->mutex);
    for (size_t i = 0; i < shown; i++) {
        remember(scanner, devices[i].address);
        csv_field(scanner->log, timestamp);
        fputc(',', scanner->log);
        csv_field(scanner->log, devices[i].address);
        fputc(',', scanner->log);
        csv_field(scanner->log, devices[i].name);
        fputc(',', scanner->log);
        if (devices[i].has_rssi)
            fprintf(scanner->log, "%d", devices[i].rssi);
        fputc(',', scanner->log);
        if (has_fix)
            fprintf(scanner->log, "%.7f,%.7f", latitude, longitude);
        else
            fputc(',', scanner->log);
        fputc('\n', scanner->log);
    }
    int flush_failed = fflush(scanner->log) != 0;
    pthread_mutex_lock(&scanner->state->mutex);
    Snapshot *snapshot = &scanner->state->snapshot;
    memcpy(snapshot->bluetooth, devices, shown * sizeof(BluetoothDevice));
    snapshot->bluetooth_shown = shown;
    snapshot->bluetooth_visible = shown;
    snapshot->bluetooth_unique = scanner->seen_count;
    snapshot->bluetooth_scans++;
    snprintf(snapshot->bluetooth_last_scan, sizeof(snapshot->bluetooth_last_scan), "%s", timestamp);
    snprintf(snapshot->bluetooth_error, sizeof(snapshot->bluetooth_error), "%s",
             flush_failed ? "Could not flush Bluetooth CSV log" : "");
    pthread_mutex_unlock(&scanner->state->mutex);
    free(output);
}

void *bluetooth_scan_loop(void *argument)
{
    BluetoothScanner *scanner = argument;
    while (!stop_requested) {
        bluetooth_scan_once(scanner);
        for (int i = 0; i < 12 && !stop_requested; i++)
            sleep(1);
    }
    return NULL;
}
