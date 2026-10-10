#include "scan.h"
#include "gps.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int valid_bssid(const char *text)
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
    return text[17] == ' ' || text[17] == '\t' || text[17] == '(' || text[17] == '\0';
}

static void save_network(Scanner *scanner, Snapshot *result, Network *network,
                         int privacy, int rsn, int wpa, int sae,
                         const char *timestamp)
{
    if (network->bssid[0] == '\0')
        return;
    if (network->ssid[0] == '\0')
        snprintf(network->ssid, sizeof(network->ssid), "<hidden>");
    snprintf(network->security, sizeof(network->security), "%s",
             sae ? "WPA3" : rsn ? "WPA2" : wpa ? "WPA" : privacy ? "WEP" : "Open");
    result->visible++;
    if (result->shown < MAX_NETWORKS)
        result->networks[result->shown++] = *network;
    int known = 0;
    for (size_t i = 0; i < scanner->seen_count; i++) {
        if (strcmp(scanner->seen[i], network->bssid) == 0) {
            known = 1;
            break;
        }
    }
    if (!known && scanner->seen_count == scanner->seen_capacity) {
        size_t next = scanner->seen_capacity ? scanner->seen_capacity * 2 : 256;
        void *grown = realloc(scanner->seen, next * sizeof(*scanner->seen));
        if (grown) {
            scanner->seen = grown;
            scanner->seen_capacity = next;
        }
    }
    if (!known && scanner->seen_count < scanner->seen_capacity) {
        memcpy(scanner->seen[scanner->seen_count++], network->bssid, 18);
    }
    csv_field(scanner->log, timestamp);
    fputc(',', scanner->log);
    csv_field(scanner->log, network->bssid);
    fputc(',', scanner->log);
    csv_field(scanner->log, network->ssid);
    fprintf(scanner->log, ",%d,%.2f,", network->frequency, network->signal);
    csv_field(scanner->log, network->security);
    fputc(',', scanner->log);
    if (result->has_fix)
        fprintf(scanner->log, "%.7f,%.7f", result->latitude, result->longitude);
    else
        fputc(',', scanner->log);
    fputc('\n', scanner->log);
}

void scan_once(Scanner *scanner)
{
    const char *const command[] = {"iw", "dev", scanner->interface, "scan", "passive", NULL};
    char *output = malloc(2 * 1024 * 1024);
    if (!output) {
        pthread_mutex_lock(&scanner->state->mutex);
        snprintf(scanner->state->snapshot.error, sizeof(scanner->state->snapshot.error),
                 "Not enough memory for Wi-Fi scan");
        pthread_mutex_unlock(&scanner->state->mutex);
        return;
    }
    double latitude = 0, longitude = 0;
    int has_fix = gps_get_fix(&latitude, &longitude);
    int code = run_command(command, output, 2 * 1024 * 1024);
    Snapshot result = {0};
    utc_now(result.last_scan);
    result.has_fix = has_fix;
    result.latitude = latitude;
    result.longitude = longitude;
    if (code != 0) {
        snprintf(result.error, sizeof(result.error), "Wi-Fi scan failed (code %d): %.175s", code, output);
        pthread_mutex_lock(&scanner->state->mutex);
        Snapshot *current = &scanner->state->snapshot;
        current->scans++;
        current->shown = 0;
        current->visible = 0;
        snprintf(current->last_scan, sizeof(current->last_scan), "%s", result.last_scan);
        snprintf(current->error, sizeof(current->error), "%s", result.error);
        current->has_fix = has_fix;
        current->latitude = latitude;
        current->longitude = longitude;
        pthread_mutex_unlock(&scanner->state->mutex);
        free(output);
        return;
    }
    Network network = {0};
    int privacy = 0, rsn = 0, wpa = 0, sae = 0;
    int active = 0;
    char *saveptr;
    for (char *line = strtok_r(output, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        if (strncmp(line, "BSS ", 4) == 0) {
            if (active)
                save_network(scanner, &result, &network, privacy, rsn, wpa, sae, result.last_scan);
            memset(&network, 0, sizeof(network));
            privacy = rsn = wpa = sae = 0;
            active = valid_bssid(line + 4);
            if (active) {
                memcpy(network.bssid, line + 4, 17);
                network.bssid[17] = '\0';
                for (int i = 0; i < 17; i++)
                    network.bssid[i] = (char)tolower((unsigned char)network.bssid[i]);
            }
            continue;
        }
        if (!active)
            continue;
        while (*line == '\t' || *line == ' ')
            line++;
        if (strncmp(line, "SSID:", 5) == 0) {
            const char *name = line + 5;
            if (*name == ' ')
                name++;
            snprintf(network.ssid, sizeof(network.ssid), "%s", name);
        } else if (strncmp(line, "freq:", 5) == 0) {
            network.frequency = atoi(line + 5);
        } else if (strncmp(line, "signal:", 7) == 0) {
            network.signal = strtod(line + 7, NULL);
        } else if (strncmp(line, "capability:", 11) == 0 && strstr(line, "Privacy")) {
            privacy = 1;
        } else if (strncmp(line, "RSN:", 4) == 0) {
            rsn = 1;
        } else if (strncmp(line, "WPA:", 4) == 0) {
            wpa = 1;
        } else if (strstr(line, "Authentication suites:") && strstr(line, "SAE")) {
            sae = 1;
        }
    }
    if (active)
        save_network(scanner, &result, &network, privacy, rsn, wpa, sae, result.last_scan);
    if (fflush(scanner->log) != 0)
        snprintf(result.error, sizeof(result.error), "Could not flush CSV log");
    pthread_mutex_lock(&scanner->state->mutex);
    Snapshot *current = &scanner->state->snapshot;
    memcpy(current->networks, result.networks, result.shown * sizeof(Network));
    current->shown = result.shown;
    current->visible = result.visible;
    current->unique = scanner->seen_count;
    current->scans++;
    current->has_fix = result.has_fix;
    current->latitude = result.latitude;
    current->longitude = result.longitude;
    snprintf(current->last_scan, sizeof(current->last_scan), "%s", result.last_scan);
    snprintf(current->error, sizeof(current->error), "%s", result.error);
    pthread_mutex_unlock(&scanner->state->mutex);
    free(output);
}

void *scan_loop(void *argument)
{
    Scanner *scanner = argument;
    while (!stop_requested) {
        scan_once(scanner);
        for (int i = 0; i < 10 && !stop_requested; i++)
            sleep(1);
    }
    return NULL;
}
