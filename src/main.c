#include "ap.h"
#include "app.h"
#include "bluetooth.h"
#include "scan.h"
#include "web.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int valid_interface(const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > 15)
        return 0;
    for (size_t i = 0; i < length; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-' && name[i] != '.')
            return 0;
    }
    return 1;
}

static int printable(const char *value, size_t minimum, size_t maximum)
{
    size_t length = strlen(value);
    if (length < minimum || length > maximum)
        return 0;
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)value[i] < 32 || (unsigned char)value[i] > 126)
            return 0;
    }
    return 1;
}

static FILE *open_csv(const char *path, const char *header)
{
    FILE *file = fopen(path, "a+");
    if (!file) {
        perror(path);
        return NULL;
    }
    struct stat info;
    if (fstat(fileno(file), &info) != 0 ||
        (info.st_size == 0 && (fputs(header, file) == EOF || fflush(file) != 0))) {
        perror(path);
        fclose(file);
        return NULL;
    }
    return file;
}

int main(int argc, char **argv)
{
    const char *ap_interface = argc > 1 ? argv[1] : "wlan0";
    const char *scan_interface = argc > 2 ? argv[2] : "wlan1";
    const char *password = getenv("WARDRIVE_PASSWORD");
    const char *ssid = getenv("WARDRIVE_SSID");
    if (!ssid)
        ssid = "loner";
    if (argc > 3 || !valid_interface(ap_interface) || !valid_interface(scan_interface) ||
        strcmp(ap_interface, scan_interface) == 0) {
        fprintf(stderr, "Usage: sudo WARDRIVE_PASSWORD='strong-password' ./wardriver [ap-interface scan-interface]\n");
        return 1;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "Run as root with sudo to create the hotspot and scan.\n");
        return 1;
    }
    if (!password || !printable(password, 8, 63) || !printable(ssid, 1, 32)) {
        fprintf(stderr, "Set WARDRIVE_PASSWORD to 8-63 printable ASCII characters; SSID must be 1-32 printable ASCII characters.\n");
        return 1;
    }
    umask(0077);
    FILE *log = open_csv("wardrive.csv",
                         "timestamp_utc,bssid,ssid,frequency_mhz,signal_dbm,security,latitude,longitude\n");
    if (!log)
        return 1;
    FILE *btlog = open_csv("bluetooth.csv",
                           "timestamp_utc,address,name,rssi_dbm,latitude,longitude\n");
    if (!btlog) {
        fclose(log);
        return 1;
    }
    int restore_managed = 0;
    char error[256] = {0};
    if (scanner_prepare(scan_interface, &restore_managed, error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        scanner_restore(scan_interface, restore_managed);
        fclose(btlog);
        fclose(log);
        return 1;
    }
    char profile[48];
    snprintf(profile, sizeof(profile), "wardrive-%ld", (long)getpid());
    char ip[48] = {0};
    int ap_active = 0;
    if (ap_start(ap_interface, ssid, password, profile, ip, sizeof(ip),
                 &ap_active, error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        if (ap_active)
            ap_stop(profile);
        scanner_restore(scan_interface, restore_managed);
        fclose(btlog);
        fclose(log);
        return 1;
    }
    AppState state = {0};
    pthread_mutex_init(&state.mutex, NULL);
    snprintf(state.snapshot.ap_ip, sizeof(state.snapshot.ap_ip), "%s", ip);
    snprintf(state.snapshot.ap_ssid, sizeof(state.snapshot.ap_ssid), "%s", ssid);
    snprintf(state.snapshot.scan_interface, sizeof(state.snapshot.scan_interface), "%s", scan_interface);
    Scanner scanner = {.state = &state, .interface = scan_interface, .log = log};
    BluetoothScanner bluetooth = {.state = &state, .log = btlog};
    struct sigaction action = {.sa_handler = request_stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    pthread_t wifi_worker, bluetooth_worker;
    if (pthread_create(&wifi_worker, NULL, scan_loop, &scanner) != 0) {
        fprintf(stderr, "Could not start Wi-Fi scan thread\n");
        ap_stop(profile);
        scanner_restore(scan_interface, restore_managed);
        pthread_mutex_destroy(&state.mutex);
        fclose(btlog);
        fclose(log);
        return 1;
    }
    int bluetooth_started = pthread_create(&bluetooth_worker, NULL, bluetooth_scan_loop, &bluetooth) == 0;
    if (!bluetooth_started) {
        pthread_mutex_lock(&state.mutex);
        snprintf(state.snapshot.bluetooth_error, sizeof(state.snapshot.bluetooth_error),
                 "Could not start Bluetooth scan thread");
        pthread_mutex_unlock(&state.mutex);
    }
    printf("loner hotspot: %s on %s\n", ssid, ap_interface);
    printf("Dashboard: http://%s:8080/\n", ip);
    printf("Wi-Fi: %s; logs: wardrive.csv, bluetooth.csv\n", scan_interface);
    printf("Press Ctrl+C to stop and remove the hotspot.\n");
    fflush(stdout);
    int web_result = web_serve(&state, ip, 8080);
    if (web_result != 0)
        fprintf(stderr, "Could not serve dashboard at %s:8080: %s\n", ip, strerror(errno));
    stop_requested = 1;
    pthread_join(wifi_worker, NULL);
    if (bluetooth_started)
        pthread_join(bluetooth_worker, NULL);
    free(scanner.seen);
    free(bluetooth.seen);
    ap_stop(profile);
    scanner_restore(scan_interface, restore_managed);
    pthread_mutex_destroy(&state.mutex);
    fclose(btlog);
    fclose(log);
    return web_result == 0 ? 0 : 1;
}
