/*
 * error.c - Comprehensive Error Handling System for Reasons DSL
 *
 * Features:
 * - Error code definitions
 * - Error message formatting
 * - Stack traces
 * - Error chaining
 * - Source code location tracking
 * - Memory error tracking
 * - Custom error types
 * - Error context preservation
 * - Thread-safe error handling
 */

#include "utils/error.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include "utils/collections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <execinfo.h>
#include <pthread.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct ErrorEntry {
    ErrorCode code;
    char *message;
    SourceLocation location;
    struct ErrorEntry *next;
} ErrorEntry;

struct ErrorContext {
    ErrorEntry *head;
    ErrorEntry *tail;
    pthread_mutex_t lock;
    bool enabled;
};

/* ======== GLOBAL VARIABLES ======== */

static __thread ErrorContext *thread_error_context = NULL;
static ErrorContext global_error_context = {0};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const char* get_error_code_string(ErrorCode code) {
    switch (code) {
        case ERROR_NONE:          return "No error";
        case ERROR_MEMORY:        return "Memory allocation failure";
        case ERROR_SYNTAX:        return "Syntax error";
        case ERROR_TYPE:          return "Type error";
        case ERROR_RUNTIME:       return "Runtime error";
        case ERROR_FILE_IO:       return "File I/O error";
        case ERROR_NETWORK:       return "Network error";
        case ERROR_DIV_ZERO:      return "Division by zero";
        case ERROR_OVERFLOW:      return "Arithmetic overflow";
        case ERROR_UNDERFLOW:     return "Arithmetic underflow";
        case ERROR_BOUNDS:        return "Index out of bounds";
        case ERROR_NULL_PTR:      return "Null pointer dereference";
        case ERROR_UNSUPPORTED:   return "Unsupported operation";
        case ERROR_ARGUMENT:      return "Invalid argument";
        case ERROR_CONFIG:        return "Configuration error";
        case ERROR_COMPILE:       return "Compilation error";
        case ERROR_IO:            return "Input/output error";
        case ERROR_TIMEOUT:       return "Operation timed out";
        case ERROR_INTERNAL:      return "Internal error";
        case ERROR_ABORT:         return "Operation aborted";
        case ERROR_LIMIT:         return "Resource limit exceeded";
        case ERROR_AUTH:          return "Authentication error";
        case ERROR_PERMISSION:    return "Permission denied";
        case ERROR_FORMAT:        return "Format error";
        case ERROR_PROTOCOL:      return "Protocol error";
        case ERROR_CONNECTION:    return "Connection error";
        case ERROR_EOF:           return "End of file";
        case ERROR_NOT_FOUND:     return "Resource not found";
        case ERROR_ALREADY_EXISTS:return "Resource already exists";
        case ERROR_BUSY:          return "Resource busy";
        case ERROR_INVALID_STATE: return "Invalid state";
        case ERROR_DEADLOCK:      return "Deadlock detected";
        case ERROR_CRYPTO:        return "Cryptographic error";
        case ERROR_JSON_SYNTAX:   return "JSON syntax error";
        case ERROR_JSON_TYPE:     return "JSON type error";
        case ERROR_CSV_FORMAT:    return "CSV format error";
        case ERROR_VALIDATION:    return "Validation error";
        case ERROR_TEST_FAILED:   return "Test assertion failed";
        case ERROR_DEPRECATED:    return "Deprecated feature used";
        default:                  return "Unknown error";
    }
}

static ErrorContext* get_error_context() {
    if (thread_error_context) {
        return thread_error_context;
    }
    return &global_error_context;
}

static void add_error_entry(ErrorContext *ctx, ErrorCode code, 
                           const char *message, const char *file, int line) {
    if (!ctx || !ctx->enabled) return;
    
    ErrorEntry *entry = mem_alloc(sizeof(ErrorEntry));
    if (!entry) return;
    
    entry->code = code;
    entry->message = message ? string_dup(message) : NULL;
    entry->location.file = file ? string_dup(file) : NULL;
    entry->location.line = line;
    entry->next = NULL;
    
    pthread_mutex_lock(&ctx->lock);
    
    if (!ctx->head) {
        ctx->head = ctx->tail = entry;
    } else {
        ctx->tail->next = entry;
        ctx->tail = entry;
    }
    
    pthread_mutex_unlock(&ctx->lock);
}

static void free_error_entry(ErrorEntry *entry) {
    if (!entry) return;
    if (entry->message) mem_free(entry->message);
    if (entry->location.file) mem_free((void*)entry->location.file);
    mem_free(entry);
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

Error* error_create(ErrorCode code, const char *message, const char *file, int line) {
    Error *err = mem_alloc(sizeof(Error));
    if (!err) return NULL;
    
    err->code = code;
    err->message = message ? string_dup(message) : NULL;
    err->location.file = file ? string_dup(file) : NULL;
    err->location.line = line;
    err->cause = NULL;
    err->stack_size = 0;
    err->stack = NULL;
    
    // Capture stack trace (up to 128 frames)
    void *buffer[128];
    int frames = backtrace(buffer, 128);
    if (frames > 0) {
        err->stack = mem_calloc(frames, sizeof(void*));
        if (err->stack) {
            memcpy(err->stack, buffer, frames * sizeof(void*));
            err->stack_size = frames;
        }
    }
    
    return err;
}

Error* error_createf(ErrorCode code, const char *file, int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *message = string_vformat(format, args);
    va_end(args);
    
    Error *err = error_create(code, message, file, line);
    if (message) mem_free(message);
    return err;
}

void error_free(Error *err) {
    if (!err) return;
    
    if (err->message) mem_free(err->message);
    if (err->location.file) mem_free((void*)err->location.file);
    if (err->stack) mem_free(err->stack);
    if (err->cause) error_free(err->cause);
    mem_free(err);
}

const char* error_message(ErrorCode code) {
    return get_error_code_string(code);
}

void error_set(ErrorCode code, const char *message, const char *file, int line) {
    ErrorContext *ctx = get_error_context();
    add_error_entry(ctx, code, message, file, line);
}

void error_setf(ErrorCode code, const char *file, int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *message = string_vformat(format, args);
    va_end(args);
    
    error_set(code, message, file, line);
    if (message) mem_free(message);
}

Error* error_get() {
    ErrorContext *ctx = get_error_context();
    if (!ctx || !ctx->head) return NULL;
    
    pthread_mutex_lock(&ctx->lock);
    ErrorEntry *entry = ctx->head;
    ctx->head = ctx->head->next;
    if (!ctx->head) ctx->tail = NULL;
    pthread_mutex_unlock(&ctx->lock);
    
    Error *err = error_create(entry->code, entry->message, 
                             entry->location.file, entry->location.line);
    free_error_entry(entry);
    return err;
}

void error_clear() {
    ErrorContext *ctx = get_error_context();
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->lock);
    ErrorEntry *current = ctx->head;
    while (current) {
        ErrorEntry *next = current->next;
        free_error_entry(current);
        current = next;
    }
    ctx->head = ctx->tail = NULL;
    pthread_mutex_unlock(&ctx->lock);
}

void error_print(Error *err, FILE *out) {
    if (!err) return;
    
    fprintf(out, "Error [%d]: %s\n", err->code, get_error_code_string(err->code));
    if (err->message) {
        fprintf(out, "  Message: %s\n", err->message);
    }
    if (err->location.file) {
        fprintf(out, "  Location: %s:%d\n", err->location.file, err->location.line);
    }
    
    if (err->stack_size > 0) {
        fprintf(out, "  Stack trace:\n");
        char **symbols = backtrace_symbols(err->stack, err->stack_size);
        if (symbols) {
            for (int i = 0; i < err->stack_size; i++) {
                fprintf(out, "    %s\n", symbols[i]);
            }
            mem_free(symbols);
        }
    }
    
    if (err->cause) {
        fprintf(out, "Caused by:\n");
        error_print(err->cause, out);
    }
}

void error_init_context(ErrorContext *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(ErrorContext));
    pthread_mutex_init(&ctx->lock, NULL);
    ctx->enabled = true;
}

void error_destroy_context(ErrorContext *ctx) {
    if (!ctx) return;
    error_clear();
    pthread_mutex_destroy(&ctx->lock);
}

void error_set_thread_context(ErrorContext *ctx) {
    thread_error_context = ctx;
}

void error_enable(bool enabled) {
    ErrorContext *ctx = get_error_context();
    if (ctx) ctx->enabled = enabled;
}
