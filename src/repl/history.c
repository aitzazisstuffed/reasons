/*
 * history.c - Command History Management for Reasons REPL
 * 
 * Features:
 * - Persistent command history across sessions
 * - History search and filtering
 * - History navigation (up/down arrows)
 * - History deduplication
 * - Configurable history size
 * - History saving/loading to file
 * - Session-based history
 * - History expansion
 * - History statistics
 * - Secure history handling
 */

#include "repl/history.h"
#include "utils/string_utils.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/collections.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* History file location */
#ifndef HISTORY_FILE
#define HISTORY_FILE ".reasons_history"
#endif

/* Maximum history size */
#ifndef MAX_HISTORY_SIZE
#define MAX_HISTORY_SIZE 1000
#endif

/* History entry structure */
typedef struct {
    char *command;          // The command string
    time_t timestamp;       // When the command was executed
    unsigned session_id;    // REPL session ID
} HistoryEntry;

/* History state structure */
struct HistoryState {
    vector_t *entries;      // History entries
    unsigned next_id;       // Next entry ID
    unsigned current_index; // Current index during navigation
    unsigned session_id;    // Current session ID
    bool enabled;           // History enabled flag
};

/* Static instance for global history */
static HistoryState *global_history = NULL;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static HistoryState* create_history_state() {
    HistoryState *hist = mem_alloc(sizeof(HistoryState));
    if (hist) {
        hist->entries = vector_create(50);
        hist->next_id = 1;
        hist->current_index = 0;
        hist->session_id = 1;
        hist->enabled = true;
    }
    return hist;
}

static HistoryEntry* create_history_entry(const char *command) {
    if (!command || *command == '\0') return NULL;
    
    HistoryEntry *entry = mem_alloc(sizeof(HistoryEntry));
    if (entry) {
        entry->command = string_duplicate(command);
        entry->timestamp = time(NULL);
        entry->session_id = global_history ? global_history->session_id : 1;
    }
    return entry;
}

static void destroy_history_entry(void *entry) {
    HistoryEntry *he = (HistoryEntry*)entry;
    if (he) {
        if (he->command) mem_free(he->command);
        mem_free(he);
    }
}

static int history_compare(const void *a, const void *b) {
    const HistoryEntry *entry_a = *(const HistoryEntry**)a;
    const HistoryEntry *entry_b = *(const HistoryEntry**)b;
    return (int)(entry_b->timestamp - entry_a->timestamp); // Newest first
}

static bool should_save_command(const char *command) {
    if (!command) return false;
    
    // Skip empty commands
    if (*command == '\0') return false;
    
    // Skip REPL commands starting with '.' except certain ones
    if (*command == '.') {
        const char *skip_commands[] = {"help", "env", "history", "license", "version"};
        for (size_t i = 0; i < sizeof(skip_commands)/sizeof(skip_commands[0]); i++) {
            if (strncmp(command + 1, skip_commands[i], strlen(skip_commands[i])) == 0) {
                return true;
            }
        }
        return false;
    }
    
    // Skip shell commands
    if (*command == '!') return false;
    
    return true;
}

static bool is_duplicate_command(const char *command) {
    if (!global_history || !command) return false;
    
    // Check last few entries to avoid duplicates
    size_t count = vector_size(global_history->entries);
    size_t start = count > 5 ? count - 5 : 0;
    
    for (size_t i = start; i < count; i++) {
        HistoryEntry *entry = vector_at(global_history->entries, i);
        if (entry && strcmp(entry->command, command) == 0) {
            return true;
        }
    }
    
    return false;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

void history_init() {
    if (!global_history) {
        global_history = create_history_state();
        history_load(HISTORY_FILE);
    }
}

void history_shutdown() {
    if (global_history) {
        history_save(HISTORY_FILE);
        
        // Free all entries
        for (size_t i = 0; i < vector_size(global_history->entries); i++) {
            HistoryEntry *entry = vector_at(global_history->entries, i);
            destroy_history_entry(entry);
        }
        vector_destroy(global_history->entries);
        mem_free(global_history);
        global_history = NULL;
    }
}

void history_add(const char *command) {
    if (!global_history || !global_history->enabled || !command) return;
    
    // Skip commands that shouldn't be saved
    if (!should_save_command(command)) return;
    
    // Skip duplicates
    if (is_duplicate_command(command)) return;
    
    // Create new entry
    HistoryEntry *entry = create_history_entry(command);
    if (!entry) return;
    
    // Add to history
    vector_append(global_history->entries, entry);
    global_history->next_id++;
    
    // Trim history to max size
    if (vector_size(global_history->entries) > MAX_HISTORY_SIZE) {
        HistoryEntry *oldest = vector_at(global_history->entries, 0);
        vector_remove(global_history->entries, 0);
        destroy_history_entry(oldest);
    }
    
    // Reset navigation index
    global_history->current_index = vector_size(global_history->entries);
}

const char* history_prev() {
    if (!global_history || vector_size(global_history->entries) == 0) {
        return NULL;
    }
    
    if (global_history->current_index > 0) {
        global_history->current_index--;
    }
    
    if (global_history->current_index < vector_size(global_history->entries)) {
        HistoryEntry *entry = vector_at(global_history->entries, global_history->current_index);
        return entry->command;
    }
    
    return NULL;
}

const char* history_next() {
    if (!global_history || vector_size(global_history->entries) == 0) {
        return NULL;
    }
    
    if (global_history->current_index < vector_size(global_history->entries) - 1) {
        global_history->current_index++;
        HistoryEntry *entry = vector_at(global_history->entries, global_history->current_index);
        return entry->command;
    }
    
    // Clear input when at the end
    global_history->current_index = vector_size(global_history->entries);
    return "";
}

void history_reset_navigation() {
    if (global_history) {
        global_history->current_index = vector_size(global_history->entries);
    }
}

bool history_save(const char *filename) {
    if (!global_history || !filename) return false;
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        LOG_ERROR("Could not open history file for writing: %s", filename);
        return false;
    }
    
    // Write header
    fprintf(file, "# Reasons REPL Command History\n");
    fprintf(file, "# Saved at: %ld\n", (long)time(NULL));
    fprintf(file, "# Format: <timestamp>|<session>|<command>\n");
    
    // Write entries
    for (size_t i = 0; i < vector_size(global_history->entries); i++) {
        HistoryEntry *entry = vector_at(global_history->entries, i);
        fprintf(file, "%ld|%u|%s\n", (long)entry->timestamp, 
                entry->session_id, entry->command);
    }
    
    fclose(file);
    return true;
}

bool history_load(const char *filename) {
    if (!global_history || !filename) return false;
    
    // Check if file exists
    struct stat st;
    if (stat(filename, &st) != 0) {
        LOG_DEBUG("History file not found: %s", filename);
        return false;
    }
    
    // Open file
    FILE *file = fopen(filename, "r");
    if (!file) {
        LOG_ERROR("Could not open history file: %s", filename);
        return false;
    }
    
    char line[1024];
    unsigned count = 0;
    
    // Read file line by line
    while (fgets(line, sizeof(line), file)) {
        // Skip comment lines
        if (*line == '#') continue;
        
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Parse fields: timestamp|session|command
        char *timestamp_str = strtok(line, "|");
        char *session_str = strtok(NULL, "|");
        char *command = strtok(NULL, "");
        
        if (!timestamp_str || !session_str || !command) continue;
        
        // Create entry
        HistoryEntry *entry = mem_alloc(sizeof(HistoryEntry));
        if (!entry) continue;
        
        entry->timestamp = atol(timestamp_str);
        entry->session_id = atoi(session_str);
        entry->command = string_duplicate(command);
        
        // Add to history
        vector_append(global_history->entries, entry);
        count++;
        
        // Update next ID
        if (entry->session_id >= global_history->session_id) {
            global_history->session_id = entry->session_id + 1;
        }
    }
    
    fclose(file);
    global_history->next_id += count;
    
    // Sort by timestamp (newest first)
    vector_sort(global_history->entries, history_compare);
    
    LOG_INFO("Loaded %u history entries from %s", count, filename);
    return true;
}

void history_clear() {
    if (global_history) {
        for (size_t i = 0; i < vector_size(global_history->entries); i++) {
            HistoryEntry *entry = vector_at(global_history->entries, i);
            destroy_history_entry(entry);
        }
        vector_clear(global_history->entries);
        global_history->current_index = 0;
        global_history->next_id = 1;
    }
}

vector_t* history_search(const char *pattern) {
    if (!global_history || !pattern || !*pattern) return NULL;
    
    vector_t *results = vector_create(10);
    if (!results) return NULL;
    
    for (size_t i = 0; i < vector_size(global_history->entries); i++) {
        HistoryEntry *entry = vector_at(global_history->entries, i);
        if (strstr(entry->command, pattern) != NULL) {
            vector_append(results, entry->command);
        }
    }
    
    return results;
}

vector_t* history_get_all() {
    if (!global_history) return NULL;
    
    vector_t *commands = vector_create(vector_size(global_history->entries));
    if (!commands) return NULL;
    
    for (size_t i = 0; i < vector_size(global_history->entries); i++) {
        HistoryEntry *entry = vector_at(global_history->entries, i);
        vector_append(commands, entry->command);
    }
    
    return commands;
}

vector_t* history_get_last(unsigned count) {
    if (!global_history) return NULL;
    
    size_t total = vector_size(global_history->entries);
    size_t start = total > count ? total - count : 0;
    
    vector_t *last_commands = vector_create(count);
    if (!last_commands) return NULL;
    
    for (size_t i = start; i < total; i++) {
        HistoryEntry *entry = vector_at(global_history->entries, i);
        vector_append(last_commands, entry->command);
    }
    
    return last_commands;
}

void history_set_enabled(bool enabled) {
    if (global_history) {
        global_history->enabled = enabled;
    }
}

bool history_is_enabled() {
    return global_history ? global_history->enabled : false;
}

unsigned history_count() {
    return global_history ? vector_size(global_history->entries) : 0;
}

unsigned history_session_id() {
    return global_history ? global_history->session_id : 0;
}

void history_remove_entry(unsigned index) {
    if (!global_history || index >= vector_size(global_history->entries)) return;
    
    HistoryEntry *entry = vector_at(global_history->entries, index);
    vector_remove(global_history->entries, index);
    destroy_history_entry(entry);
    
    // Adjust navigation index
    if (global_history->current_index > index) {
        global_history->current_index--;
    }
}

const char* history_expand(const char *input) {
    if (!input || !*input || !global_history) return input;
    
    // History expansion: !! - last command, !n - command number n
    if (*input == '!') {
        if (input[1] == '!') {
            // Last command
            if (vector_size(global_history->entries) > 0) {
                HistoryEntry *last = vector_back(global_history->entries);
                return last->command;
            }
        } else if (isdigit(input[1])) {
            // Command by number
            unsigned index = atoi(input + 1);
            if (index > 0 && index <= vector_size(global_history->entries)) {
                HistoryEntry *entry = vector_at(global_history->entries, index - 1);
                return entry->command;
            }
        } else {
            // Search for command starting with pattern
            const char *pattern = input + 1;
            for (int i = vector_size(global_history->entries) - 1; i >= 0; i--) {
                HistoryEntry *entry = vector_at(global_history->entries, i);
                if (strncmp(entry->command, pattern, strlen(pattern)) == 0) {
                    return entry->command;
                }
            }
        }
    }
    
    // No expansion performed
    return input;
}

void history_print_stats(FILE *output) {
    if (!global_history || !output) return;
    
    time_t first_time = 0, last_time = 0;
    unsigned command_count = 0;
    
    if (vector_size(global_history->entries) > 0) {
        HistoryEntry *first = vector_at(global_history->entries, 0);
        HistoryEntry *last = vector_back(global_history->entries);
        first_time = first->timestamp;
        last_time = last->timestamp;
        command_count = vector_size(global_history->entries);
    }
    
    fprintf(output, "Command History Statistics:\n");
    fprintf(output, "  Total commands:    %u\n", command_count);
    
    if (command_count > 0) {
        char first_buf[64], last_buf[64];
        struct tm *tm_first = localtime(&first_time);
        struct tm *tm_last = localtime(&last_time);
        
        strftime(first_buf, sizeof(first_buf), "%Y-%m-%d %H:%M:%S", tm_first);
        strftime(last_buf, sizeof(last_buf), "%Y-%m-%d %H:%M:%S", tm_last);
        
        fprintf(output, "  First command:     %s\n", first_buf);
        fprintf(output, "  Last command:      %s\n", last_buf);
        
        double days = difftime(last_time, first_time) / (60 * 60 * 24);
        double commands_per_day = days > 0 ? command_count / days : command_count;
        
        fprintf(output, "  Timespan:          %.1f days\n", days);
        fprintf(output, "  Commands per day:  %.1f\n", commands_per_day);
    }
}
