#define _GNU_SOURCE
#include "web.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static char *load_page(void)
{
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || (size_t)length >= sizeof(executable) - 1)
        return NULL;
    executable[length] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash)
        return NULL;
    size_t remaining = (size_t)(executable + sizeof(executable) - slash - 1);
    if (snprintf(slash + 1, remaining, "assets/dashboard.html") >= (int)remaining)
        return NULL;
    FILE *file = fopen(executable, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 1 || size > 256 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *page = malloc((size_t)size + 1);
    if (!page) {
        fclose(file);
        return NULL;
    }
    if (fread(page, 1, (size_t)size, file) != (size_t)size) {
        free(page);
        fclose(file);
        return NULL;
    }
    page[size] = '\0';
    fclose(file);
    return page;
}

static void json_string(FILE *stream, const char *value)
{
    fputc('"', stream);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', stream);
            fputc(*p, stream);
        } else if (*p < 32) {
            fprintf(stream, "\\u%04x", *p);
        } else {
            fputc(*p, stream);
        }
    }
    fputc('"', stream);
}

static char *status_json(AppState *state)
{
    Snapshot *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot)
        return NULL;
    pthread_mutex_lock(&state->mutex);
    *snapshot = state->snapshot;
    pthread_mutex_unlock(&state->mutex);
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    if (!stream) {
        free(snapshot);
        return NULL;
    }
    fprintf(stream, "{\"visible\":%zu,\"unique\":%zu,\"scans\":%lu,\"last_scan\":",
            snapshot->visible, snapshot->unique, snapshot->scans);
    json_string(stream, snapshot->last_scan);
    fputs(",\"error\":", stream);
    json_string(stream, snapshot->error);
    fputs(",\"ap_ssid\":", stream);
    json_string(stream, snapshot->ap_ssid);
    fputs(",\"ap_ip\":", stream);
    json_string(stream, snapshot->ap_ip);
    fputs(",\"scan_interface\":", stream);
    json_string(stream, snapshot->scan_interface);
    if (snapshot->has_fix)
        fprintf(stream, ",\"gps\":{\"lat\":%.7f,\"lon\":%.7f}",
                snapshot->latitude, snapshot->longitude);
    else
        fputs(",\"gps\":null", stream);
    fputs(",\"networks\":[", stream);
    for (size_t i = 0; i < snapshot->shown; i++) {
        Network *network = &snapshot->networks[i];
        if (i)
            fputc(',', stream);
        fputs("{\"ssid\":", stream);
        json_string(stream, network->ssid);
        fputs(",\"bssid\":", stream);
        json_string(stream, network->bssid);
        fprintf(stream, ",\"signal\":%.2f,\"frequency\":%d,\"security\":",
                network->signal, network->frequency);
        json_string(stream, network->security);
        fputc('}', stream);
    }
    fprintf(stream,
            "],\"bluetooth\":{\"visible\":%zu,\"unique\":%zu,\"scans\":%lu,\"last_scan\":",
            snapshot->bluetooth_visible, snapshot->bluetooth_unique, snapshot->bluetooth_scans);
    json_string(stream, snapshot->bluetooth_last_scan);
    fputs(",\"error\":", stream);
    json_string(stream, snapshot->bluetooth_error);
    fputs(",\"devices\":[", stream);
    for (size_t i = 0; i < snapshot->bluetooth_shown; i++) {
        BluetoothDevice *device = &snapshot->bluetooth[i];
        if (i)
            fputc(',', stream);
        fputs("{\"address\":", stream);
        json_string(stream, device->address);
        fputs(",\"name\":", stream);
        json_string(stream, device->name);
        fputs(",\"rssi\":", stream);
        if (device->has_rssi)
            fprintf(stream, "%d", device->rssi);
        else
            fputs("null", stream);
        fputc('}', stream);
    }
    fputs("]}}", stream);
    if (fclose(stream) != 0) {
        free(buffer);
        buffer = NULL;
    }
    free(snapshot);
    return buffer;
}

static void send_all(int fd, const char *text, size_t length)
{
    while (length) {
        ssize_t sent = send(fd, text, length, MSG_NOSIGNAL);
        if (sent <= 0)
            return;
        text += sent;
        length -= (size_t)sent;
    }
}

static void handle_client(int fd, AppState *state, const char *page)
{
    char request[2048];
    ssize_t count = recv(fd, request, sizeof(request) - 1, 0);
    if (count <= 0)
        return;
    request[count] = '\0';
    char method[16], path[128];
    int parsed = sscanf(request, "%15s %127s", method, path);
    const char *type = "text/plain; charset=utf-8";
    const char *status = "200 OK";
    const char *body = "Not found\n";
    char *dynamic = NULL;
    if (parsed == 2 && strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        type = "text/html; charset=utf-8";
        body = page;
    } else if (parsed == 2 && strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        type = "application/json; charset=utf-8";
        dynamic = status_json(state);
        if (dynamic)
            body = dynamic;
        else {
            status = "500 Internal Server Error";
            body = "{}";
        }
    } else {
        status = "404 Not Found";
    }
    char header[512];
    int size = snprintf(header, sizeof(header),
                        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                        "Connection: close\r\n\r\n", status, type, strlen(body));
    if (size > 0 && (size_t)size < sizeof(header)) {
        send_all(fd, header, (size_t)size);
        send_all(fd, body, strlen(body));
    }
    free(dynamic);
}

int web_serve(AppState *state, const char *ip, int port)
{
    char *page = load_page();
    if (!page) {
        errno = ENOENT;
        return -1;
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        free(page);
        return -1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((unsigned short)port)};
    if (inet_pton(AF_INET, ip, &address.sin_addr) != 1 ||
        bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 16) != 0) {
        close(server);
        free(page);
        return -1;
    }
    while (!stop_requested) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(server, &set);
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
        int ready = select(server + 1, &set, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0) {
            close(server);
            free(page);
            return -1;
        }
        if (ready == 0)
            continue;
        int client = accept(server, NULL, NULL);
        if (client < 0)
            continue;
        struct timeval limit = {.tv_sec = 2, .tv_usec = 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
        handle_client(client, state, page);
        close(client);
    }
    close(server);
    free(page);
    return 0;
}
