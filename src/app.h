#ifndef APP_H
#define APP_H

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_NETWORKS 256
#define MAX_BLUETOOTH 128

typedef struct {
    char address[18];
    char name[128];
    int rssi;
    int has_rssi;
} BluetoothDevice;

typedef struct {
    char bssid[18];
    char ssid[128];
    char security[16];
    int frequency;
    double signal;
} Network;

typedef struct {
    Network networks[MAX_NETWORKS];
    size_t shown;
    size_t visible;
    size_t unique;
    unsigned long scans;
    int has_fix;
    double latitude;
    double longitude;
    char last_scan[32];
    char error[256];
    char ap_ip[48];
    char ap_ssid[33];
    char scan_interface[16];
    BluetoothDevice bluetooth[MAX_BLUETOOTH];
    size_t bluetooth_shown;
    size_t bluetooth_visible;
    size_t bluetooth_unique;
    unsigned long bluetooth_scans;
    char bluetooth_last_scan[32];
    char bluetooth_error[256];
} Snapshot;

typedef struct {
    pthread_mutex_t mutex;
    Snapshot snapshot;
} AppState;

typedef struct {
    AppState *state;
    const char *interface;
    FILE *log;
    char (*seen)[18];
    size_t seen_count;
    size_t seen_capacity;
} Scanner;

typedef struct {
    AppState *state;
    FILE *log;
    char (*seen)[18];
    size_t seen_count;
    size_t seen_capacity;
} BluetoothScanner;

extern volatile sig_atomic_t stop_requested;

#endif
