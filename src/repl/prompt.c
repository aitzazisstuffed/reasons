/*
 * prompt.c - Dynamic Prompt Generation for Reasons REPL
 * 
 * Features:
 * - Context-aware prompt customization
 * - Multiple prompt levels (primary, secondary, debug)
 * - Real-time variable expansion
 * - Color support
 * - Git integration
 * - Session information
 * - Execution status indicators
 * - Configurable prompt formats
 * - Environment variable support
 * - History-based information
 * - Performance optimization
 */

#include "repl/prompt.h"
#include "repl/history.h"
#include "reasons/repl.h"
#include "reasons/runtime.h"
#include "reasons/debugger.h"
#include "utils/string_utils.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/collections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <limits.h>

/* ANSI color codes */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_BOLD    "\x1b[1m"

/* Default prompt formats */
#define DEFAULT_PRIMARY_PROMPT   ANSI_BOLD ANSI_GREEN "reasons> " ANSI_RESET
#define DEFAULT_SECONDARY_PROMPT ANSI_BOLD ANSI_YELLOW "...> " ANSI_RESET
#define DEFAULT_DEBUG_PROMPT     ANSI_BOLD ANSI_RED "debug> " ANSI_RESET

/* Git status refresh interval (seconds) */
#define GIT_REFRESH_INTERVAL 5

/* Prompt configuration structure */
struct PromptConfig {
    char *primary_format;      // Primary prompt format
    char *secondary_format;    // Secondary prompt format
    char *debug_format;        // Debug mode prompt format
    bool show_colors;          // Enable/disable colors
    bool show_git_info;        // Enable/disable git info
    time_t last_git_check;     // Last time git status was checked
    char *cached_git_branch;   // Cached git branch name
    char *cached_git_status;   // Cached git status
};

/* Static configuration instance */
static PromptConfig *config = NULL;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static void init_default_config() {
    if (config) return;
    
    config = mem_alloc(sizeof(PromptConfig));
    if (config) {
        config->primary_format = string_duplicate(DEFAULT_PRIMARY_PROMPT);
        config->secondary_format = string_duplicate(DEFAULT_SECONDARY_PROMPT);
        config->debug_format = string_duplicate(DEFAULT_DEBUG_PROMPT);
        config->show_colors = true;
        config->show_git_info = true;
        config->last_git_check = 0;
        config->cached_git_branch = NULL;
        config->cached_git_status = NULL;
    }
}

static char* get_current_dir() {
    static char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) {
        return cwd;
    }
    return "?";
}

static char* get_dir_basename() {
    char *cwd = get_current_dir();
    char *basename = strrchr(cwd, '/');
    return basename ? basename + 1 : cwd;
}

static char* get_username() {
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "?";
}

static char* get_hostname() {
    static char hostname[HOST_NAME_MAX];
    if (gethostname(hostname, sizeof(hostname)) {
        return "?";
    }
    // Remove domain part if present
    char *dot = strchr(hostname, '.');
    if (dot) *dot = '\0';
    return hostname;
}

static char* get_time() {
    static char time_buf[16];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm);
    return time_buf;
}

static char* get_git_branch() {
    if (!config->show_git_info) return NULL;
    
    // Check if we need to refresh
    time_t now = time(NULL);
    if (config->cached_git_branch && (now - config->last_git_check) < GIT_REFRESH_INTERVAL) {
        return config->cached_git_branch;
    }
    
    // Free previous cache
    if (config->cached_git_branch) {
        mem_free(config->cached_git_branch);
        config->cached_git_branch = NULL;
    }
    
    // Check if we're in a git repo
    struct stat st;
    if (stat(".git", &st) != 0) {
        config->last_git_check = now;
        return NULL;
    }
    
    // Get branch name
    FILE *fp = popen("git branch --show-current 2>/dev/null", "r");
    if (!fp) return NULL;
    
    char branch[128];
    if (fgets(branch, sizeof(branch), fp)) {
        // Remove newline
        size_t len = strlen(branch);
        if (len > 0 && branch[len-1] == '\n') {
            branch[len-1] = '\0';
        }
        config->cached_git_branch = string_duplicate(branch);
    }
    
    pclose(fp);
    config->last_git_check = now;
    return config->cached_git_branch;
}

static char* get_git_status() {
    if (!config->show_git_info) return NULL;
    
    char *branch = get_git_branch();
    if (!branch) return NULL;
    
    // Check if we need to refresh status
    time_t now = time(NULL);
    if (config->cached_git_status && (now - config->last_git_check) < GIT_REFRESH_INTERVAL) {
        return config->cached_git_status;
    }
    
    // Free previous cache
    if (config->cached_git_status) {
        mem_free(config->cached_git_status);
        config->cached_git_status = NULL;
    }
    
    // Get git status
    FILE *fp = popen("git status --porcelain 2>/dev/null | wc -l", "r");
    if (!fp) return NULL;
    
    char status[32];
    if (fgets(status, sizeof(status), fp)) {
        int change_count = atoi(status);
        if (change_count > 0) {
            config->cached_git_status = string_duplicate("*");
        } else {
            config->cached_git_status = string_duplicate("");
        }
    }
    
    pclose(fp);
    return config->cached_git_status;
}

static void expand_variable(char var, char **buffer, size_t *buf_size, size_t *pos, REPLState *repl) {
    char value[128] = {0};
    
    switch (var) {
        case 'u': // Username
            snprintf(value, sizeof(value), "%s", get_username());
            break;
        case 'h': // Hostname
            snprintf(value, sizeof(value), "%s", get_hostname());
            break;
        case 'd': // Current directory (full)
            snprintf(value, sizeof(value), "%s", get_current_dir());
            break;
        case 'w': // Current directory (basename)
            snprintf(value, sizeof(value), "%s", get_dir_basename());
            break;
        case 't': // Time (HH:MM:SS)
            snprintf(value, sizeof(value), "%s", get_time());
            break;
        case 'n': // Line number
            snprintf(value, sizeof(value), "%u", repl ? repl->line_count : 0);
            break;
        case 's': // Session ID
            snprintf(value, sizeof(value), "%u", history_session_id());
            break;
        case 'g': // Git branch
            if (char *branch = get_git_branch()) {
                snprintf(value, sizeof(value), "%s", branch);
            }
            break;
        case 'm': // Git modification status
            if (char *status = get_git_status()) {
                snprintf(value, sizeof(value), "%s", status);
            }
            break;
        case 'c': // Command count
            snprintf(value, sizeof(value), "%u", history_count());
            break;
        case 'e': // Last command success
            snprintf(value, sizeof(value), "%s", repl && repl->last_error ? "!" : "");
            break;
        case 'v': // Version
            snprintf(value, sizeof(value), "%s", REASONS_VERSION);
            break;
        case 'l': // Current script name
            if (repl && repl->current_script) {
                char *base = strrchr(repl->current_script, '/');
                snprintf(value, sizeof(value), "%s", base ? base + 1 : repl->current_script);
            }
            break;
        case 'D': // Debugger indicator
            snprintf(value, sizeof(value), "%s", repl && repl->debug_mode ? "DEBUG" : "");
            break;
        case '%': // Literal %
            snprintf(value, sizeof(value), "%%");
            break;
        default:
            // Unknown variable, include as-is
            snprintf(value, sizeof(value), "%%%c", var);
            break;
    }
    
    // Append to buffer
    size_t val_len = strlen(value);
    if (*pos + val_len >= *buf_size) {
        *buf_size *= 2;
        *buffer = mem_realloc(*buffer, *buf_size);
    }
    strcat(*buffer, value);
    *pos += val_len;
}

static char* expand_prompt_format(const char *format, REPLState *repl) {
    if (!format) return NULL;
    
    size_t buf_size = 256;
    size_t pos = 0;
    char *buffer = mem_alloc(buf_size);
    if (!buffer) return NULL;
    buffer[0] = '\0';
    
    for (const char *p = format; *p; p++) {
        if (*p == '%') {
            // Expand variable
            char var = *(++p);
            if (var) {
                expand_variable(var, &buffer, &buf_size, &pos, repl);
            }
        } else {
            // Regular character
            if (pos + 1 >= buf_size) {
                buf_size *= 2;
                buffer = mem_realloc(buffer, buf_size);
            }
            buffer[pos++] = *p;
            buffer[pos] = '\0';
        }
    }
    
    return buffer;
}

static char* get_prompt_format(REPLState *repl) {
    if (!config) init_default_config();
    
    if (repl->input_buffer && vector_size(repl->input_buffer) > 0) {
        // Secondary prompt for multi-line input
        return config->secondary_format;
    } else if (repl->debug_mode) {
        // Debug mode prompt
        return config->debug_format;
    } else {
        // Primary prompt
        return config->primary_format;
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

char* generate_prompt(REPLState *repl) {
    if (!repl) return string_duplicate("> ");
    
    const char *format = get_prompt_format(repl);
    char *prompt = expand_prompt_format(format, repl);
    
    // Fallback if expansion fails
    if (!prompt) {
        prompt = string_duplicate(repl->debug_mode ? "debug> " : "reasons> ");
    }
    
    return prompt;
}

void prompt_init(REPLState *repl) {
    // Initialize configuration
    init_default_config();
    
    // Apply environment variable settings if available
    const char *env_prompt = getenv("REASONS_PROMPT");
    if (env_prompt) {
        prompt_set_primary_format(env_prompt);
    }
    
    const char *env_secondary = getenv("REASONS_SECONDARY_PROMPT");
    if (env_secondary) {
        prompt_set_secondary_format(env_secondary);
    }
    
    const char *env_debug = getenv("REASONS_DEBUG_PROMPT");
    if (env_debug) {
        prompt_set_debug_format(env_debug);
    }
}

void prompt_cleanup() {
    if (config) {
        if (config->primary_format) mem_free(config->primary_format);
        if (config->secondary_format) mem_free(config->secondary_format);
        if (config->debug_format) mem_free(config->debug_format);
        if (config->cached_git_branch) mem_free(config->cached_git_branch);
        if (config->cached_git_status) mem_free(config->cached_git_status);
        mem_free(config);
        config = NULL;
    }
}

void prompt_set_primary_format(const char *format) {
    if (!config) init_default_config();
    
    if (config->primary_format) mem_free(config->primary_format);
    config->primary_format = format ? string_duplicate(format) : string_duplicate(DEFAULT_PRIMARY_PROMPT);
}

void prompt_set_secondary_format(const char *format) {
    if (!config) init_default_config();
    
    if (config->secondary_format) mem_free(config->secondary_format);
    config->secondary_format = format ? string_duplicate(format) : string_duplicate(DEFAULT_SECONDARY_PROMPT);
}

void prompt_set_debug_format(const char *format) {
    if (!config) init_default_config();
    
    if (config->debug_format) mem_free(config->debug_format);
    config->debug_format = format ? string_duplicate(format) : string_duplicate(DEFAULT_DEBUG_PROMPT);
}

const char* prompt_get_primary_format() {
    if (!config) init_default_config();
    return config->primary_format;
}

const char* prompt_get_secondary_format() {
    if (!config) init_default_config();
    return config->secondary_format;
}

const char* prompt_get_debug_format() {
    if (!config) init_default_config();
    return config->debug_format;
}

void prompt_enable_colors(bool enable) {
    if (!config) init_default_config();
    config->show_colors = enable;
}

bool prompt_colors_enabled() {
    if (!config) init_default_config();
    return config->show_colors;
}

void prompt_enable_git_info(bool enable) {
    if (!config) init_default_config();
    config->show_git_info = enable;
}

bool prompt_git_info_enabled() {
    if (!config) init_default_config();
    return config->show_git_info;
}

void prompt_refresh() {
    if (!config) return;
    
    // Invalidate git cache
    config->last_git_check = 0;
    if (config->cached_git_branch) {
        mem_free(config->cached_git_branch);
        config->cached_git_branch = NULL;
    }
    if (config->cached_git_status) {
        mem_free(config->cached_git_status);
        config->cached_git_status = NULL;
    }
}

const char* prompt_get_help() {
    return "Prompt Format Variables:\n"
           "  %u - Username\n"
           "  %h - Hostname (short)\n"
           "  %d - Current directory (full)\n"
           "  %w - Current directory (basename)\n"
           "  %t - Time (HH:MM:SS)\n"
           "  %n - Line number\n"
           "  %s - Session ID\n"
           "  %g - Git branch\n"
           "  %m - Git modification status (* if changes)\n"
           "  %c - Command count\n"
           "  %e - Error indicator (! if last command failed)\n"
           "  %v - Version\n"
           "  %l - Current script name\n"
           "  %D - Debugger indicator\n"
           "  %% - Literal %\n"
           "\n"
           "Example: \"%u@%h:%w [%n] %D> \"\n";
}
