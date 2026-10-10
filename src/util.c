#include "util.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int run_command(const char *const args[], char *output, size_t capacity)
{
    int pipefd[2];
    pid_t child;
    size_t used = 0;
    int status;
    char buffer[4096];
    ssize_t count;

    if (capacity == 0)
        return -1;
    output[0] = '\0';
    if (pipe(pipefd) != 0)
        return -1;
    child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (child == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(args[0], (char *const *)args);
        dprintf(STDERR_FILENO, "Cannot execute %s: %s\n", args[0], strerror(errno));
        _exit(127);
    }
    close(pipefd[1]);
    for (;;) {
        count = read(pipefd[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        size_t copy = (size_t)count;
        if (copy > capacity - 1 - used)
            copy = capacity - 1 - used;
        if (copy) {
            memcpy(output + used, buffer, copy);
            used += copy;
        }
    }
    close(pipefd[0]);
    output[used] = '\0';
    do {
        count = waitpid(child, &status, 0);
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

void utc_now(char output[32])
{
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(output, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void csv_field(FILE *file, const char *value)
{
    fputc('"', file);
    if (*value == '=' || *value == '+' || *value == '-' || *value == '@' || *value == '\t')
        fputc('\'', file);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"')
            fputc('"', file);
        if (*p == '\r' || *p == '\n')
            fputc(' ', file);
        else
            fputc(*p, file);
    }
    fputc('"', file);
}
