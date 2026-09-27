#include "log.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static log_level_t current_level = LOG_LEVEL_INFO;
static FILE* log_file = NULL;
static char log_filename[256] = {0};
static int log_max_size = 10 * 1024 * 1024;
static int log_rotate_count = 5;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* log_level_names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

int log_init(log_config_t* config) {
    if (!config) {
        return -1;
    }
    
    pthread_mutex_lock(&log_mutex);
    
    current_level = config->level;
    
    if (config->filename && strlen(config->filename) > 0) {
        strncpy(log_filename, config->filename, sizeof(log_filename) - 1);
        log_file = fopen(log_filename, "a");
        if (!log_file) {
            log_file = stdout;
        }
    } else {
        log_file = stdout;
    }
    
    if (config->max_size > 0) {
        log_max_size = config->max_size;
    }
    
    if (config->rotate_count > 0) {
        log_rotate_count = config->rotate_count;
    }
    
    pthread_mutex_unlock(&log_mutex);
    
    LOG_INFO("LOG", "Log system initialized, level=%s", log_level_names[current_level]);
    return 0;
}

void log_deinit(void) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file && log_file != stdout && log_file != stderr) {
        fclose(log_file);
        log_file = NULL;
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_set_level(log_level_t level) {
    pthread_mutex_lock(&log_mutex);
    current_level = level;
    pthread_mutex_unlock(&log_mutex);
}

static void log_rotate(void) {
    if (!log_file || log_file == stdout || log_file == stderr) {
        return;
    }
    
    fseek(log_file, 0, SEEK_END);
    long size = ftell(log_file);
    
    if (size >= log_max_size) {
        fclose(log_file);
        
        char backup[512];
        for (int i = log_rotate_count - 1; i > 0; i--) {
            snprintf(backup, sizeof(backup), "%s.%d", log_filename, i);
            char prev[512];
            snprintf(prev, sizeof(prev), "%s.%d", log_filename, i - 1);
            rename(prev, backup);
        }
        
        char first_backup[512];
        snprintf(first_backup, sizeof(first_backup), "%s.0", log_filename);
        rename(log_filename, first_backup);
        
        log_file = fopen(log_filename, "a");
        if (!log_file) {
            log_file = stdout;
        }
    }
}

void log_write(log_level_t level, const char* module, const char* format, ...) {
    pthread_mutex_lock(&log_mutex);
    
    if (level < current_level) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    
    log_rotate();
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(log_file, "[%s] [%s] [%s] ", time_str, log_level_names[level], module ? module : "UNKNOWN");
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}