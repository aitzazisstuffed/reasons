/*
 * history.c - Decision History Tracking for Reasons Debugger
 * 
 * Features:
 * - Records decision nodes visited during execution
 * - Tracks decision values and timestamps
 * - Supports history filtering and querying
 * - Provides detailed history inspection
 * - Implements history export functionality
 * - Integrates with debugger and runtime
 */

#include "reasons/debugger.h"
#include "reasons/tree.h"
#include "reasons/runtime.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include "stdlib/datetime.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct DecisionRecord {
    char *node_id;              // Node identifier
    char *node_description;     // Human-readable description
    reasons_value_t decision;    // Value at decision point
    time_t timestamp;           // Time of decision
    double execution_time;      // Execution time in ms
    bool is_leaf;               // Is this a leaf node?
    bool is_condition;          // Is this a condition node?
    unsigned depth;             // Depth in decision tree
    unsigned sequence;          // Execution sequence number
} DecisionRecord;

struct DecisionHistory {
    vector_t *records;          // Vector of DecisionRecord pointers
    unsigned sequence_counter;  // Global execution sequence
    bool enabled;               // Tracking enabled state
    bool detailed;              // Record detailed information
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static DecisionRecord* create_record(TreeNode *node, reasons_value_t decision) {
    if (!node) return NULL;
    
    DecisionRecord *record = mem_alloc(sizeof(DecisionRecord));
    if (!record) {
        LOG_ERROR("Failed to allocate memory for decision record");
        return NULL;
    }
    
    // Initialize basic information
    record->node_id = node->id ? string_duplicate(node->id) : NULL;
    record->node_description = node->description ? string_duplicate(node->description) : NULL;
    record->decision = reasons_value_clone(&decision);
    record->timestamp = time(NULL);
    record->execution_time = 0.0;
    record->is_leaf = (node->type == NODE_LEAF);
    record->is_condition = (node->type == NODE_CONDITION);
    record->depth = 0;  // Will be set later
    record->sequence = 0;
    
    return record;
}

static void destroy_record(DecisionRecord *record) {
    if (!record) return;
    
    if (record->node_id) mem_free(record->node_id);
    if (record->node_description) mem_free(record->node_description);
    reasons_value_free(&record->decision);
    mem_free(record);
}

static void update_depth_info(DecisionHistory *history, TreeNode *node) {
    if (!history || !node) return;
    
    // Calculate depth by traversing up the tree
    unsigned depth = 0;
    TreeNode *current = node;
    while (current->parent) {
        depth++;
        current = current->parent;
    }
    
    // Set depth for the last record
    if (vector_size(history->records) {
        DecisionRecord *last = vector_back(history->records);
        last->depth = depth;
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

DecisionHistory* history_create() {
    DecisionHistory *history = mem_alloc(sizeof(DecisionHistory));
    if (history) {
        history->records = vector_create(32);
        history->sequence_counter = 0;
        history->enabled = true;
        history->detailed = true;
    }
    return history;
}

void history_destroy(DecisionHistory *history) {
    if (!history) return;
    
    // Free all records
    for (size_t i = 0; i < vector_size(history->records); i++) {
        DecisionRecord *record = vector_at(history->records, i);
        destroy_record(record);
    }
    vector_destroy(history->records);
    mem_free(history);
}

void history_record_decision(DecisionHistory *history, TreeNode *node, 
                            reasons_value_t decision, double exec_time) {
    if (!history || !history->enabled || !node) return;
    
    DecisionRecord *record = create_record(node, decision);
    if (!record) return;
    
    record->execution_time = exec_time;
    record->sequence = history->sequence_counter++;
    
    // Update depth information
    update_depth_info(history, node);
    
    if (!vector_append(history->records, record)) {
        LOG_ERROR("Failed to add decision record to history");
        destroy_record(record);
    }
}

void history_clear(DecisionHistory *history) {
    if (!history) return;
    
    for (size_t i = 0; i < vector_size(history->records); i++) {
        DecisionRecord *record = vector_at(history->records, i);
        destroy_record(record);
    }
    vector_clear(history->records);
    history->sequence_counter = 0;
}

size_t history_count(DecisionHistory *history) {
    return history ? vector_size(history->records) : 0;
}

void history_set_enabled(DecisionHistory *history, bool enabled) {
    if (history) history->enabled = enabled;
}

bool history_is_enabled(DecisionHistory *history) {
    return history ? history->enabled : false;
}

void history_set_detail_level(DecisionHistory *history, bool detailed) {
    if (history) history->detailed = detailed;
}

const DecisionRecord* history_get_record(DecisionHistory *history, size_t index) {
    if (!history || index >= vector_size(history->records)) {
        return NULL;
    }
    return vector_at(history->records, index);
}

vector_t* history_find_records(DecisionHistory *history, const char *node_id) {
    if (!history || !node_id) return NULL;
    
    vector_t *results = vector_create(4);
    if (!results) return NULL;
    
    for (size_t i = 0; i < vector_size(history->records); i++) {
        DecisionRecord *record = vector_at(history->records, i);
        if (record->node_id && strcmp(record->node_id, node_id) == 0) {
            vector_append(results, record);
        }
    }
    
    return results;
}

void history_print(DecisionHistory *history, FILE *output, int max_records) {
    if (!history || !output) return;
    
    size_t start_idx = 0;
    size_t count = vector_size(history->records);
    
    if (max_records > 0 && (size_t)max_records < count) {
        start_idx = count - max_records;
    }
    
    fprintf(output, "Decision History (%zu entries):\n", count);
    fprintf(output, "Seq | Timestamp           | Depth | Node ID          | Decision\n");
    fprintf(output, "----+---------------------+-------+------------------+-----------------\n");
    
    for (size_t i = start_idx; i < count; i++) {
        DecisionRecord *record = vector_at(history->records, i);
        char time_buf[32];
        char decision_buf[64];
        struct tm *tm = localtime(&record->timestamp);
        
        // Format timestamp
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
        
        // Format decision value
        reasons_value_format(&record->decision, decision_buf, sizeof(decision_buf));
        
        fprintf(output, "%4d | %s | %5u | %-16s | %s\n",
                record->sequence,
                time_buf,
                record->depth,
                record->node_id ? record->node_id : "<none>",
                decision_buf);
    }
}

void history_export_json(DecisionHistory *history, FILE *output) {
    if (!history || !output) return;
    
    fprintf(output, "{\n");
    fprintf(output, "  \"history\": [\n");
    
    for (size_t i = 0; i < vector_size(history->records); i++) {
        DecisionRecord *record = vector_at(history->records, i);
        char time_buf[32];
        char decision_buf[128];
        struct tm *tm = localtime(&record->timestamp);
        
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", tm);
        reasons_value_to_json(&record->decision, decision_buf, sizeof(decision_buf));
        
        fprintf(output, "    {\n");
        fprintf(output, "      \"sequence\": %u,\n", record->sequence);
        fprintf(output, "      \"timestamp\": \"%s\",\n", time_buf);
        fprintf(output, "      \"execution_time\": %.3f,\n", record->execution_time);
        fprintf(output, "      \"node_id\": \"%s\",\n", record->node_id ? record->node_id : "");
        if (record->node_description) {
            fprintf(output, "      \"description\": \"%s\",\n", record->node_description);
        }
        fprintf(output, "      \"depth\": %u,\n", record->depth);
        fprintf(output, "      \"is_leaf\": %s,\n", record->is_leaf ? "true" : "false");
        fprintf(output, "      \"is_condition\": %s,\n", record->is_condition ? "true" : "false");
        fprintf(output, "      \"decision\": %s\n", decision_buf);
        
        // Comma handling for JSON array
        if (i < vector_size(history->records) - 1) {
            fprintf(output, "    },\n");
        } else {
            fprintf(output, "    }\n");
        }
    }
    
    fprintf(output, "  ]\n");
    fprintf(output, "}\n");
}

const DecisionRecord* history_last_decision(DecisionHistory *history) {
    if (!history || vector_size(history->records) == 0) {
        return NULL;
    }
    return vector_back(history->records);
}

vector_t* history_get_path(DecisionHistory *history, size_t index) {
    if (!history || index >= vector_size(history->records)) {
        return NULL;
    }
    
    vector_t *path = vector_create(16);
    if (!path) return NULL;
    
    // Walk backwards to build decision path
    for (int i = (int)index; i >= 0; i--) {
        DecisionRecord *record = vector_at(history->records, i);
        vector_prepend(path, record);
        
        // Stop when we reach the root (depth 0)
        if (record->depth == 0) break;
    }
    
    return path;
}

double history_get_total_time(DecisionHistory *history) {
    if (!history) return 0.0;
    
    double total = 0.0;
    for (size_t i = 0; i < vector_size(history->records); i++) {
        DecisionRecord *record = vector_at(history->records, i);
        total += record->execution_time;
    }
    return total;
}
