#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4
} log_level_t;

typedef struct {
    log_level_t level;
    FILE* file;
    char filename[256];
    int max_size;
    int rotate_count;
} log_config_t;

int log_init(log_config_t* config);

void log_deinit(void);

void log_set_level(log_level_t level);

void log_write(log_level_t level, const char* module, const char* format, ...);

#define LOG_DEBUG(module, format, ...) log_write(LOG_LEVEL_DEBUG, module, format, ##__VA_ARGS__)
#define LOG_INFO(module, format, ...) log_write(LOG_LEVEL_INFO, module, format, ##__VA_ARGS__)
#define LOG_WARN(module, format, ...) log_write(LOG_LEVEL_WARN, module, format, ##__VA_ARGS__)
#define LOG_ERROR(module, format, ...) log_write(LOG_LEVEL_ERROR, module, format, ##__VA_ARGS__)
#define LOG_FATAL(module, format, ...) log_write(LOG_LEVEL_FATAL, module, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif