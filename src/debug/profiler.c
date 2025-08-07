/*
 * profiler.c - Performance Profiler for Reasons DSL
 * 
 * Features:
 * - Fine-grained execution timing
 * - Node-level performance metrics
 * - Function call profiling
 * - Memory allocation tracking
 * - Bottleneck identification
 * - Call graph analysis
 * - Multi-run statistics
 * - Text and JSON report generation
 * - Integration with debugger and runtime
 */

#include "reasons/debugger.h"
#include "reasons/tree.h"
#include "reasons/runtime.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include "stdlib/stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

/* ======== STRUCTURE DEFINITIONS ======== */

typedef enum {
    PROFILE_NODE,
    PROFILE_FUNCTION,
    PROFILE_BLOCK
} ProfileEntryType;

typedef struct ProfileEntry {
    const char *id;             // Node ID or function name
    ProfileEntryType type;      // Entry type
    unsigned call_count;        // Number of calls
    double total_time;          // Total execution time (ms)
    double min_time;            // Minimum execution time (ms)
    double max_time;            // Maximum execution time (ms)
    double start_time;          // Start time of current call
    unsigned depth;             // Call depth
    bool is_active;             // Is currently being profiled?
    struct ProfileEntry *parent;// Parent in call tree
    vector_t *children;         // Child entries
} ProfileEntry;

struct Profiler {
    hash_table_t *entries;      // Profile entries (key: id)
    vector_t *call_stack;       // Current call stack
    vector_t *entry_list;       // All entries in order of first appearance
    ProfileEntry *current;      // Current active entry
    unsigned depth;             // Current call depth
    size_t total_allocations;   // Total memory allocated
    size_t total_frees;         // Total memory freed
    size_t peak_memory;         // Peak memory usage
    size_t start_memory;        // Memory at profiler start
    double total_time;          // Total profiled time
    double start_time;          // Profiling session start time
    unsigned sample_count;      // Number of profile runs
    bool enabled;               // Profiler enabled state
    bool memory_tracking;       // Track memory allocations
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static double get_current_time() {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart * 1000.0; // ms
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

static ProfileEntry* create_profile_entry(const char *id, ProfileEntryType type) {
    ProfileEntry *entry = mem_alloc(sizeof(ProfileEntry));
    if (entry) {
        entry->id = string_duplicate(id);
        entry->type = type;
        entry->call_count = 0;
        entry->total_time = 0.0;
        entry->min_time = DBL_MAX;
        entry->max_time = 0.0;
        entry->start_time = 0.0;
        entry->depth = 0;
        entry->is_active = false;
        entry->parent = NULL;
        entry->children = vector_create(4);
    }
    return entry;
}

static void destroy_profile_entry(void *data) {
    ProfileEntry *entry = (ProfileEntry*)data;
    if (entry) {
        if (entry->id) mem_free((void*)entry->id);
        if (entry->children) vector_destroy(entry->children);
        mem_free(entry);
    }
}

static ProfileEntry* get_or_create_entry(Profiler *prof, const char *id, ProfileEntryType type) {
    if (!prof || !id) return NULL;
    
    // Get existing entry
    ProfileEntry *entry = hash_get(prof->entries, id);
    if (entry) return entry;
    
    // Create new entry
    entry = create_profile_entry(id, type);
    if (entry) {
        hash_set(prof->entries, entry->id, entry);
        vector_append(prof->entry_list, entry);
    }
    return entry;
}

static void update_call_hierarchy(Profiler *prof, ProfileEntry *entry) {
    if (!prof || !entry) return;
    
    // Set parent-child relationship
    if (prof->current && entry != prof->current) {
        // Avoid adding self as child
        entry->parent = prof->current;
        
        // Add to parent's children if not already present
        bool found = false;
        for (size_t i = 0; i < vector_size(prof->current->children); i++) {
            if (vector_at(prof->current->children, i) == entry) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            vector_append(prof->current->children, entry);
        }
    }
}

static void update_memory_stats(Profiler *prof) {
    if (!prof || !prof->memory_tracking) return;
    
    size_t current_memory = memory_current_usage();
    if (current_memory > prof->peak_memory) {
        prof->peak_memory = current_memory;
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

Profiler* profiler_create(bool enable_memory_tracking) {
    Profiler *prof = mem_alloc(sizeof(Profiler));
    if (prof) {
        prof->entries = hash_create(128, destroy_profile_entry);
        prof->call_stack = vector_create(32);
        prof->entry_list = vector_create(64);
        prof->current = NULL;
        prof->depth = 0;
        prof->total_allocations = 0;
        prof->total_frees = 0;
        prof->peak_memory = 0;
        prof->start_memory = memory_current_usage();
        prof->total_time = 0.0;
        prof->start_time = get_current_time();
        prof->sample_count = 0;
        prof->enabled = true;
        prof->memory_tracking = enable_memory_tracking;
    }
    return prof;
}

void profiler_destroy(Profiler *prof) {
    if (!prof) return;
    
    hash_destroy(prof->entries);
    vector_destroy(prof->call_stack);
    vector_destroy(prof->entry_list);
    mem_free(prof);
}

void profiler_start(Profiler *prof) {
    if (prof) {
        prof->start_time = get_current_time();
        prof->enabled = true;
        prof->start_memory = memory_current_usage();
        prof->peak_memory = prof->start_memory;
        prof->sample_count++;
    }
}

void profiler_stop(Profiler *prof) {
    if (prof) {
        prof->total_time += get_current_time() - prof->start_time;
        prof->enabled = false;
        update_memory_stats(prof);
    }
}

void profiler_reset(Profiler *prof) {
    if (!prof) return;
    
    // Clear all entries
    hash_clear(prof->entries);
    vector_clear(prof->call_stack);
    vector_clear(prof->entry_list);
    
    // Reset state
    prof->current = NULL;
    prof->depth = 0;
    prof->total_allocations = 0;
    prof->total_frees = 0;
    prof->peak_memory = 0;
    prof->total_time = 0.0;
    prof->sample_count = 0;
}

void profiler_begin_node(Profiler *prof, const char *node_id) {
    if (!prof || !prof->enabled || !node_id) return;
    
    ProfileEntry *entry = get_or_create_entry(prof, node_id, PROFILE_NODE);
    if (!entry) return;
    
    // Update call stack
    vector_append(prof->call_stack, prof->current);
    prof->current = entry;
    prof->depth++;
    
    // Setup entry
    entry->depth = prof->depth;
    entry->is_active = true;
    entry->start_time = get_current_time();
    entry->call_count++;
    
    // Update call hierarchy
    update_call_hierarchy(prof, entry);
    
    // Update memory stats
    update_memory_stats(prof);
}

void profiler_end_node(Profiler *prof, const char *node_id) {
    if (!prof || !prof->enabled || !node_id) return;
    
    ProfileEntry *entry = hash_get(prof->entries, node_id);
    if (!entry || !entry->is_active) return;
    
    // Calculate duration
    double end_time = get_current_time();
    double duration = end_time - entry->start_time;
    
    // Update entry stats
    entry->total_time += duration;
    if (duration < entry->min_time) entry->min_time = duration;
    if (duration > entry->max_time) entry->max_time = duration;
    entry->is_active = false;
    
    // Restore call stack
    if (vector_size(prof->call_stack)) {
        prof->current = vector_back(prof->call_stack);
        vector_pop(prof->call_stack);
    } else {
        prof->current = NULL;
    }
    prof->depth--;
    
    // Update memory stats
    update_memory_stats(prof);
}

void profiler_begin_function(Profiler *prof, const char *func_name) {
    if (!prof || !prof->enabled || !func_name) return;
    
    ProfileEntry *entry = get_or_create_entry(prof, func_name, PROFILE_FUNCTION);
    if (!entry) return;
    
    // Update call stack
    vector_append(prof->call_stack, prof->current);
    prof->current = entry;
    prof->depth++;
    
    // Setup entry
    entry->depth = prof->depth;
    entry->is_active = true;
    entry->start_time = get_current_time();
    entry->call_count++;
    
    // Update call hierarchy
    update_call_hierarchy(prof, entry);
    
    // Update memory stats
    update_memory_stats(prof);
}

void profiler_end_function(Profiler *prof, const char *func_name) {
    profiler_end_node(prof, func_name);  // Same implementation as node
}

void profiler_record_alloc(Profiler *prof, size_t size) {
    if (!prof || !prof->memory_tracking) return;
    
    prof->total_allocations += size;
    update_memory_stats(prof);
}

void profiler_record_free(Profiler *prof, size_t size) {
    if (!prof || !prof->memory_tracking) return;
    
    prof->total_frees += size;
    update_memory_stats(prof);
}

void profiler_print_report(Profiler *prof, FILE *output) {
    if (!prof || !output) return;
    
    double avg_total_time = prof->sample_count > 0 ? 
        prof->total_time / prof->sample_count : 0.0;
    
    fprintf(output, "\nPerformance Profile Report\n");
    fprintf(output, "==========================\n");
    fprintf(output, "  Total runs:        %u\n", prof->sample_count);
    fprintf(output, "  Total time:        %.3f ms\n", prof->total_time);
    fprintf(output, "  Avg. time/run:     %.3f ms\n", avg_total_time);
    
    if (prof->memory_tracking) {
        fprintf(output, "  Memory allocated:  %zu bytes\n", prof->total_allocations);
        fprintf(output, "  Memory freed:      %zu bytes\n", prof->total_frees);
        fprintf(output, "  Peak memory:       %zu bytes\n", prof->peak_memory);
        fprintf(output, "  Net memory:        %zd bytes\n", 
                (ssize_t)(prof->total_allocations - prof->total_frees));
    }
    
    fprintf(output, "\nTop Nodes/Functions by Time:\n");
    fprintf(output, "ID/Name                   Calls     Total (ms)    Avg (ms)    Min (ms)    Max (ms)    %% Time\n");
    fprintf(output, "------------------------- -------- ------------- ---------- ---------- ---------- ----------\n");
    
    // Create a copy for sorting
    vector_t *sorted = vector_copy(prof->entry_list);
    if (!sorted) return;
    
    // Sort by total time descending
    vector_sort(sorted, 
        [](const void *a, const void *b) -> int {
            const ProfileEntry *ea = *(const ProfileEntry**)a;
            const ProfileEntry *eb = *(const ProfileEntry**)b;
            if (ea->total_time > eb->total_time) return -1;
            if (ea->total_time < eb->total_time) return 1;
            return 0;
        });
    
    // Print top entries (up to 20)
    size_t count = vector_size(sorted);
    size_t max_entries = count > 20 ? 20 : count;
    for (size_t i = 0; i < max_entries; i++) {
        ProfileEntry *entry = vector_at(sorted, i);
        double avg_time = entry->call_count ? entry->total_time / entry->call_count : 0.0;
        double percent = prof->total_time > 0 ? (entry->total_time / prof->total_time) * 100.0 : 0.0;
        
        const char *type_prefix = "";
        switch (entry->type) {
            case PROFILE_FUNCTION: type_prefix = "fn:"; break;
            case PROFILE_BLOCK: type_prefix = "blk:"; break;
            default: break;
        }
        
        fprintf(output, "%-25s %8u %13.3f %10.3f %10.3f %10.3f %9.1f%%\n",
                entry->id, 
                entry->call_count,
                entry->total_time,
                avg_time,
                entry->min_time,
                entry->max_time,
                percent);
    }
    
    vector_destroy(sorted);
}

void profiler_export_json(Profiler *prof, FILE *output) {
    if (!prof || !output) return;
    
    double avg_total_time = prof->sample_count > 0 ? 
        prof->total_time / prof->sample_count : 0.0;
    
    fprintf(output, "{\n");
    fprintf(output, "  \"profiler_report\": {\n");
    fprintf(output, "    \"sample_count\": %u,\n", prof->sample_count);
    fprintf(output, "    \"total_time_ms\": %.3f,\n", prof->total_time);
    fprintf(output, "    \"average_time_ms\": %.3f,\n", avg_total_time);
    
    if (prof->memory_tracking) {
        fprintf(output, "    \"memory_allocated_bytes\": %zu,\n", prof->total_allocations);
        fprintf(output, "    \"memory_freed_bytes\": %zu,\n", prof->total_frees);
        fprintf(output, "    \"peak_memory_bytes\": %zu,\n", prof->peak_memory);
        fprintf(output, "    \"net_memory_bytes\": %zd,\n", 
                (ssize_t)(prof->total_allocations - prof->total_frees));
    }
    
    // Entries
    fprintf(output, "    \"entries\": [\n");
    for (size_t i = 0; i < vector_size(prof->entry_list); i++) {
        ProfileEntry *entry = vector_at(prof->entry_list, i);
        double avg_time = entry->call_count ? entry->total_time / entry->call_count : 0.0;
        double percent = prof->total_time > 0 ? (entry->total_time / prof->total_time) * 100.0 : 0.0;
        
        fprintf(output, "      {\n");
        fprintf(output, "        \"id\": \"%s\",\n", entry->id);
        fprintf(output, "        \"type\": \"%s\",\n", 
                entry->type == PROFILE_FUNCTION ? "function" : 
                entry->type == PROFILE_BLOCK ? "block" : "node");
        fprintf(output, "        \"call_count\": %u,\n", entry->call_count);
        fprintf(output, "        \"total_time_ms\": %.3f,\n", entry->total_time);
        fprintf(output, "        \"average_time_ms\": %.3f,\n", avg_time);
        fprintf(output, "        \"min_time_ms\": %.3f,\n", entry->min_time);
        fprintf(output, "        \"max_time_ms\": %.3f,\n", entry->max_time);
        fprintf(output, "        \"percentage\": %.1f", percent);
        
        // Children hierarchy
        if (vector_size(entry->children)) {
            fprintf(output, ",\n        \"children\": [");
            for (size_t j = 0; j < vector_size(entry->children); j++) {
                ProfileEntry *child = vector_at(entry->children, j);
                fprintf(output, "\"%s\"%s", child->id, 
                        j < vector_size(entry->children)-1 ? ", " : "");
            }
            fprintf(output, "]");
        }
        
        // Comma handling for JSON array
        if (i < vector_size(prof->entry_list) - 1) {
            fprintf(output, "\n      },\n");
        } else {
            fprintf(output, "\n      }\n");
        }
    }
    fprintf(output, "    ]\n");
    fprintf(output, "  }\n");
    fprintf(output, "}\n");
}

const ProfileEntry* profiler_get_entry(Profiler *prof, const char *id) {
    if (!prof || !id) return NULL;
    return hash_get(prof->entries, id);
}

double profiler_get_avg_time(Profiler *prof, const char *id) {
    const ProfileEntry *entry = profiler_get_entry(prof, id);
    if (!entry || !entry->call_count) return 0.0;
    return entry->total_time / entry->call_count;
}

double profiler_get_total_time(Profiler *prof, const char *id) {
    const ProfileEntry *entry = profiler_get_entry(prof, id);
    return entry ? entry->total_time : 0.0;
}

vector_t* profiler_get_slowest_entries(Profiler *prof, unsigned count) {
    if (!prof) return NULL;
    
    vector_t *slowest = vector_create(count);
    if (!slowest) return NULL;
    
    // Create a copy for sorting
    vector_t *all_entries = vector_copy(prof->entry_list);
    if (!all_entries) {
        vector_destroy(slowest);
        return NULL;
    }
    
    // Sort by total time descending
    vector_sort(all_entries, 
        [](const void *a, const void *b) -> int {
            const ProfileEntry *ea = *(const ProfileEntry**)a;
            const ProfileEntry *eb = *(const ProfileEntry**)b;
            if (ea->total_time > eb->total_time) return -1;
            if (ea->total_time < eb->total_time) return 1;
            return 0;
        });
    
    // Get top entries
    size_t max = vector_size(all_entries) < count ? vector_size(all_entries) : count;
    for (size_t i = 0; i < max; i++) {
        vector_append(slowest, vector_at(all_entries, i));
    }
    
    vector_destroy(all_entries);
    return slowest;
}

void profiler_enable_memory_tracking(Profiler *prof, bool enable) {
    if (prof) prof->memory_tracking = enable;
}

bool profiler_is_enabled(Profiler *prof) {
    return prof ? prof->enabled : false;
}

void profiler_set_enabled(Profiler *prof, bool enabled) {
    if (prof) prof->enabled = enabled;
}
