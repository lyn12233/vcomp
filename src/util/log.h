#ifndef C1_UTIL_LOG_H
#define C1_UTIL_LOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void c1_log_common_va(FILE *pipe,                                                             //
                             const char *lv, const char *file, const char *func, int line, int fail, //
                             const char *msg, va_list args) {
    time_t now = time(NULL);
    struct tm *tminf = localtime(&now);
    char tmstr[20];
    strftime(tmstr, sizeof(tmstr), "%Y-%m-%d %H-%M-%S", tminf);
    fprintf(pipe, "[%s] %s ", tmstr, lv);
    if (file)
        fprintf(pipe, "(%s:%d) ", file, line);
    if (func)
        fprintf(pipe, "(%s) ", func);
    // va_list args;
    // va_start(args, msg);
    vfprintf(pipe, msg, args);
    va_end(args);
    fprintf(pipe, "\r\n");
    if (fail) {
        fflush(pipe);
        exit(-1);
    }
}

static void __attribute__((format(printf, 7, 8)))                                     //
c1_log_common(FILE *pipe,                                                             //
              const char *lv, const char *file, const char *func, int line, int fail, //
              const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    c1_log_common_va(pipe, lv, file, func, line, fail, msg, args);
}

static void __attribute__((format(printf, 7, 8)))                                                         //
c1_log_common_noret [[noreturn]] (FILE *pipe,                                                             //
                                  const char *lv, const char *file, const char *func, int line, int fail, //
                                  const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    c1_log_common_va(pipe, lv, file, func, line, fail, msg, args);
    exit(-1);
}

#define debug(msg, ...) c1_log_common(stdout, "[DEBUG]", NULL, NULL, __LINE__, 0, msg, ##__VA_ARGS__)
#define info(msg, ...) c1_log_common(stdout, "[INFO]", NULL, NULL, __LINE__, 0, msg, ##__VA_ARGS__)
#define warning(msg, ...) c1_log_common(stdout, "[WARN]", NULL, NULL, __LINE__, 0, msg, ##__VA_ARGS__)
#define fatal(msg, ...) c1_log_common_noret(stdout, "[FATAL]", NULL, NULL, __LINE__, 1, msg, ##__VA_ARGS__)

#define debug2(msg, ...) c1_log_common(stdout, "[DEBUG]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define info2(msg, ...) c1_log_common(stdout, "[INFO]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define warning2(msg, ...) c1_log_common(stdout, "[WARN]", __FILE__, __func__, __LINE__, 0, msg, ##__VA_ARGS__)
#define fatal2(msg, ...) c1_log_common_noret(stdout, "[FATAL]", __FILE__, __func__, __LINE__, 1, msg, ##__VA_ARGS__)

#define assert_fatal(stmt)                         \
    {                                              \
        if (!(stmt))                               \
            fatal2("Assertion failed: %s", #stmt); \
    }
#define assert_fatal_ex(stmt, msg, ...)                                    \
    {                                                                      \
        if (!(stmt)) {                                                     \
            warning2("Assertion failed: %s (see extra inf below)", #stmt); \
            fatal(msg, ##__VA_ARGS__);                                     \
        }                                                                  \
    }

#ifdef __cplusplus
}
#endif
#endif