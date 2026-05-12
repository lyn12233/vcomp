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

#ifdef CONFIG_PROFILE
    #define C1_PROFILE_STEP_CNT 16
typedef struct {
    uint64_t cnt;
    uint64_t last_tm; // enter time
    uint64_t sum_tm[C1_PROFILE_STEP_CNT];
} c1_profile_t;
static void c1_profile_reset(c1_profile_t *prof) {
    *prof = (c1_profile_t){0};
}
static void c1_profile_enter(c1_profile_t *prof) {
    prof->cnt++;
    prof->last_tm = (uint64_t)clock();
}
static void c1_profile_step(c1_profile_t *prof, int step) {
    uint64_t cur_time = (uint64_t)clock();
    uint64_t last_time = prof->last_tm;
    prof->sum_tm[step] += cur_time - last_time;
    prof->last_tm = (uint64_t)clock();
}
static void c1_profile_exit(c1_profile_t *prof) {
    c1_profile_step(prof, 0);
}
static void c1_profile_repr(FILE *fp, c1_profile_t *prof, const char *nm, int nb) {
    fprintf(fp, "Profile(\"%s\", [%llu] [", nm, prof->cnt);
    for (int i = 1; i <= nb; i++) {
        fprintf(fp, "%llu|", prof->sum_tm[i]);
    }
    fprintf(fp, "%llu])", prof->sum_tm[0]);
}
#else
typedef struct {
} c1_profile_t;
static void c1_profile_reset(c1_profile_t *prof) {}
static void c1_profile_enter(c1_profile_t *prof) {}
static void c1_profile_step(c1_profile_t *prof, int step) {}
static void c1_profile_exit(c1_profile_t *prof) {}
static void c1_profile_repr(FILE *fp, c1_profile_t *prof, const char *nm, int nb) {
    fprintf(fp, "Profile([NA])");
}
#endif

#define debug(msg, ...) c1_log_common(stdout, "[DEBUG]", NULL, NULL, __LINE__, 0, msg, ##__VA_ARGS__)
#define info_(msg, ...) c1_log_common(stdout, "[INFO]", NULL, NULL, __LINE__, 0, msg, ##__VA_ARGS__)
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