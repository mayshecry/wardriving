#include "gps.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int number_field(const char *line, const char *key, double *value)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(line, needle);
    if (!p)
        return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p++ != ':')
        return 0;
    while (*p == ' ' || *p == '\t')
        p++;
    char *end;
    errno = 0;
    double parsed = strtod(p, &end);
    if (p == end || errno || !isfinite(parsed))
        return 0;
    *value = parsed;
    return 1;
}

int gps_get_fix(double *latitude, double *longitude)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(2947)};
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    const char request[] = "?WATCH={\"enable\":true,\"json\":true};\n";
    if (send(fd, request, sizeof(request) - 1, MSG_NOSIGNAL) < 0) {
        close(fd);
        return 0;
    }
    char line[4096];
    size_t used = 0;
    int found = 0;
    for (int attempt = 0; attempt < 3 && !found; attempt++) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 500000};
        int ready = select(fd + 1, &set, NULL, NULL, &timeout);
        if (ready <= 0)
            break;
        char buffer[1024];
        ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        for (ssize_t i = 0; i < count; i++) {
            if (buffer[i] == '\n') {
                line[used] = '\0';
                double mode, lat, lon;
                if (strstr(line, "\"class\":\"TPV\"") &&
                    number_field(line, "mode", &mode) && mode >= 2 &&
                    number_field(line, "lat", &lat) &&
                    number_field(line, "lon", &lon) &&
                    lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
                    *latitude = lat;
                    *longitude = lon;
                    found = 1;
                    break;
                }
                used = 0;
            } else if (used < sizeof(line) - 1) {
                line[used++] = buffer[i];
            }
        }
    }
    close(fd);
    return found;
}
