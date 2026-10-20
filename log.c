#include "utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int g_verbosity = LOG_INFO;

static const char *level_label(int level) {
    switch (level) {
        case LOG_QUIET: return "";
        case LOG_ERROR:  return "ERROR";
        case LOG_INFO:   return "INFO";
        case LOG_DEBUG:  return "DEBUG";
        default:         return "???";
    }
}

static int isatty_fd(int fd) {
    return isatty(fd) == 1;
}

void logf_at(int level, const char *fmt, ...) {
    if (level > g_verbosity) return;
    int use_tty = isatty_fd(STDERR_FILENO);
    if (use_tty && level == LOG_ERROR) fprintf(stderr, "\033[31m");
    else if (use_tty && level == LOG_INFO) fprintf(stderr, "\033[32m");
    fprintf(stderr, "[%s] ", level_label(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (use_tty) fprintf(stderr, "\033[0m");
}

/* Прогресс печатается только в TTY: в батч-скриптах stderr идёт в лог-файл,
 * и escape-последовательности там только мусорят. */
static int prev_total = 0;
static int prev_percent = -1;

void log_progress(int index, int total, const char *name) {
    if (total <= 0 || !isatty_fd(STDERR_FILENO)) return;
    if (total != prev_total) { prev_total = total; prev_percent = -1; }
    int percent = (index * 100) / total;
    if (percent == prev_percent) return;
    prev_percent = percent;
    fprintf(stderr, "\r[%3d%%] %d/%d  %-40s", percent, index, total, name ? name : "");
    if (index == total) fprintf(stderr, "\n");
    fflush(stderr);
}
