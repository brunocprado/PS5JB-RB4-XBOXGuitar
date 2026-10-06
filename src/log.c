/*
    log.c - PS5Instrumentalizer
    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ps5/klog.h>

#include "log.h"

#define LOG_MAX_SIZE (512 * 1024)

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec g_start;

void log_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start);
    mkdir(LOG_DIR, 0777);

    /* start a fresh file when the old one grew too large */
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX_SIZE) {
        FILE *f = fopen(LOG_PATH, "w");
        if (f != NULL)
            fclose(f);
    }
    FILE *f = fopen(LOG_PATH, "a");
    if (f != NULL) {
        fputs("\n==== PS5Instrumentalizer started ====\n", f);
        fclose(f);
    }
}

void log_write(const char *fmt, ...) {
    char line[768];
    char body[640];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - g_start.tv_sec) * 1000 + (now.tv_nsec - g_start.tv_nsec) / 1000000;
    snprintf(line, sizeof(line), "[%ld.%03ld] %s\n", ms / 1000, ms % 1000, body);

    pthread_mutex_lock(&g_log_mutex);
    klog_printf("[" PLUGIN_NAME "] %s", line);
    FILE *f = fopen(LOG_PATH, "a");
    if (f != NULL) {
        fputs(line, f);
        fclose(f);
    }
    pthread_mutex_unlock(&g_log_mutex);
}
