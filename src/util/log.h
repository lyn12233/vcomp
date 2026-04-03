#ifndef C1_UTIL_LOG_H
#define C1_UTIL_LOG_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void __attribute__((format(printf, 7, 8)))                                     //
c1_log_common(FILE *pipe,                                                             //
              const char *lv, const char *file, const char *func, int line, int fail, //
              const char *msg, ...) {
    time_t now = time(NULL);
    struct tm *tminf = localtime(&now);
    char tmstr[20];
    strftime(tmstr, sizeof(tmstr), "%Y-%m-%d %H-%M-%S", tminf);
    fprintf(pipe, "[%s] %s ", tmstr, lv);
    if (file)
        fprintf(pipe, "(%s:%d) ", file, line);
    if (func)
        fprintf(pipe, "(%s) ", func);
    va_list args;
    va_start(args, msg);
    vfprintf(pipe, msg, args);
    va_end(args);
    fprintf(pipe, "\r\n");
    if (fail) {
        fflush(pipe);
        exit(-1);
    }
}

#define debug(msg, ...) c1_log_common(stdout, "[DEBUG]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define info(msg, ...) c1_log_common(stdout, "[INFO]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define warning(msg, ...) c1_log_common(stdout, "[WARN]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define fatal(msg, ...) c1_log_common(stdout, "[FATAL]", __FILE__, __func__, __LINE__, 1, msg, ##__VA_ARGS__)
#define assert_fatal(stmt)                        \
    {                                             \
        if (!(stmt))                              \
            fatal("Assertion failed: %s", #stmt); \
    }

#endif