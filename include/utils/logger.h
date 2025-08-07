#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include <stdio.h>
#include <stdbool.h>

/* ======== LOG LEVEL DEFINITIONS ======== */

typedef enum {
    LOG_LEVEL_TRACE,   // Detailed trace information
    LOG_LEVEL_DEBUG,   // Debugging information
    LOG_LEVEL_INFO,    // General information
    LOG_LEVEL_WARN,    // Warnings
    LOG_LEVEL_ERROR,   // Errors
    LOG_LEVEL_FATAL    // Fatal errors (terminates application)
} LogLevel;

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct Logger Logger;

/* ======== PUBLIC API ======== */

/**
 * Initializes the global logger system
 * 
 * @param min_level Minimum log level to output
 * @param default_dest Default output destination (NULL for stderr)
 */
void logger_init(LogLevel min_level, FILE *default_dest);

/**
 * Cleans up logger resources
 */
void logger_cleanup(void);

/**
 * Adds a log destination
 * 
 * @param file File stream to write to
 * @param level Minimum log level for this destination
 * @param take_ownership True to transfer ownership (logger will close file)
 */
void logger_add_destination(FILE *file, LogLevel level, bool take_ownership);

/**
 * Configures file-based logging with rotation
 * 
 * @param filename Log file path
 * @param max_size Maximum file size before rotation (bytes)
 * @param max_files Maximum number of rotated files to keep
 */
void logger_set_file(const char *filename, size_t max_size, int max_files);

/**
 * Sets the global minimum log level
 * 
 * @param level New minimum log level
 */
void logger_set_level(LogLevel level);

/**
 * Enables or disables logging globally
 * 
 * @param enabled True to enable logging, false to disable
 */
void logger_set_enabled(bool enabled);

/**
 * Logs a message with specified level
 * 
 * @param level Log level
 * @param file Source file name (__FILE__)
 * @param line Source line number (__LINE__)
 * @param format printf-style format string
 * @param ... Format arguments
 */
void log_message(LogLevel level, const char *file, int line, const char *format, ...);

/* ======== CONVENIENCE MACROS ======== */

#define LOG_TRACE(...) log_trace(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_debug(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_warn(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) log_fatal(__FILE__, __LINE__, __VA_ARGS__)

/* ======== CONVENIENCE FUNCTIONS ======== */

void log_trace(const char *file, int line, const char *format, ...);
void log_debug(const char *file, int line, const char *format, ...);
void log_info(const char *file, int line, const char *format, ...);
void log_warn(const char *file, int line, const char *format, ...);
void log_error(const char *file, int line, const char *format, ...);
void log_fatal(const char *file, int line, const char *format, ...);

#endif /* UTILS_LOGGER_H */
