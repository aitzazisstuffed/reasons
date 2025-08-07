#ifndef UTILS_ERROR_H
#define UTILS_ERROR_H

#include <stdbool.h>
#include <stdio.h>

/* ======== ERROR CODE DEFINITIONS ======== */

typedef enum {
    ERROR_NONE,
    ERROR_MEMORY,
    ERROR_SYNTAX,
    ERROR_TYPE,
    ERROR_RUNTIME,
    ERROR_FILE_IO,
    ERROR_NETWORK,
    ERROR_DIV_ZERO,
    ERROR_OVERFLOW,
    ERROR_UNDERFLOW,
    ERROR_BOUNDS,
    ERROR_NULL_PTR,
    ERROR_UNSUPPORTED,
    ERROR_ARGUMENT,
    ERROR_CONFIG,
    ERROR_COMPILE,
    ERROR_IO,
    ERROR_TIMEOUT,
    ERROR_INTERNAL,
    ERROR_ABORT,
    ERROR_LIMIT,
    ERROR_AUTH,
    ERROR_PERMISSION,
    ERROR_FORMAT,
    ERROR_PROTOCOL,
    ERROR_CONNECTION,
    ERROR_EOF,
    ERROR_NOT_FOUND,
    ERROR_ALREADY_EXISTS,
    ERROR_BUSY,
    ERROR_INVALID_STATE,
    ERROR_DEADLOCK,
    ERROR_CRYPTO,
    ERROR_JSON_SYNTAX,
    ERROR_JSON_TYPE,
    ERROR_CSV_FORMAT,
    ERROR_VALIDATION,
    ERROR_TEST_FAILED,
    ERROR_DEPRECATED
} ErrorCode;

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    const char *file;
    int line;
} SourceLocation;

typedef struct Error {
    ErrorCode code;
    char *message;
    SourceLocation location;
    struct Error *cause;
    void **stack;
    size_t stack_size;
} Error;

typedef struct ErrorContext ErrorContext;

/* ======== PUBLIC API ======== */

/**
 * Creates a new error object
 * 
 * @param code Error code
 * @param message Error message (optional)
 * @param file Source file where error occurred
 * @param line Line number where error occurred
 * @return New error object or NULL on failure
 */
Error* error_create(ErrorCode code, const char *message, const char *file, int line);

/**
 * Creates a new error object with formatted message
 * 
 * @param code Error code
 * @param file Source file where error occurred
 * @param line Line number where error occurred
 * @param format printf-style format string
 * @param ... Format arguments
 * @return New error object or NULL on failure
 */
Error* error_createf(ErrorCode code, const char *file, int line, const char *format, ...);

/**
 * Frees an error object and all its resources
 * 
 * @param err Error object to free
 */
void error_free(Error *err);

/**
 * Gets the string representation of an error code
 * 
 * @param code Error code
 * @return Corresponding error message string
 */
const char* error_message(ErrorCode code);

/**
 * Sets an error in the current error context
 * 
 * @param code Error code
 * @param message Error message
 * @param file Source file where error occurred
 * @param line Line number where error occurred
 */
void error_set(ErrorCode code, const char *message, const char *file, int line);

/**
 * Sets an error with formatted message in the current error context
 * 
 * @param code Error code
 * @param file Source file where error occurred
 * @param line Line number where error occurred
 * @param format printf-style format string
 * @param ... Format arguments
 */
void error_setf(ErrorCode code, const char *file, int line, const char *format, ...);

/**
 * Retrieves the next error from the current error context
 * 
 * @return Error object (caller must free) or NULL if no errors
 */
Error* error_get(void);

/**
 * Clears all errors from the current error context
 */
void error_clear(void);

/**
 * Prints an error and its stack trace to a file stream
 * 
 * @param err Error to print
 * @param out Output file stream (e.g., stdout, stderr)
 */
void error_print(Error *err, FILE *out);

/**
 * Initializes an error context
 * 
 * @param ctx Error context to initialize
 */
void error_init_context(ErrorContext *ctx);

/**
 * Destroys an error context and frees its resources
 * 
 * @param ctx Error context to destroy
 */
void error_destroy_context(ErrorContext *ctx);

/**
 * Sets the error context for the current thread
 * 
 * @param ctx Error context to use for current thread
 */
void error_set_thread_context(ErrorContext *ctx);

/**
 * Enables or disables error tracking
 * 
 * @param enabled True to enable error tracking, false to disable
 */
void error_enable(bool enabled);

#endif /* UTILS_ERROR_H */
