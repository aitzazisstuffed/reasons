/*
 * trace_viz.c - Execution Trace Visualization for Reasons DSL
 * 
 * Features:
 * - Graphviz DOT format for trace visualization
 * - Timeline view of execution flow
 * - Step-by-step trace replay
 * - State diffs between steps
 * - Variable value tracking
 * - Performance metrics visualization
 * - Error highlighting
 * - Interactive trace exploration
 * - Multiple output formats
 * - Integration with explanation engine
 */

#include "reasons/viz.h"
#include "reasons/trace.h"
#include "reasons/explain.h"
#include "reasons/tree.h"
#include "reasons/runtime.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "viz/graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    bool show_state_diffs;    // Show state changes between steps
    bool show_values;         // Display variable values
    bool show_performance;    // Include performance metrics
    bool colorize;            // Use color in output
    bool compact;             // Compact representation
    bool highlight_errors;    // Highlight error states
    unsigned max_steps;       // Maximum steps to render
    const char *engine;       // Layout engine (dot, neato, etc.)
    trace_t *trace;           // Trace to visualize
    explain_engine_t *explainer; // Explanation engine
} TraceVizOptions;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const char* step_type_name(StepType type) {
    switch (type) {
        case STEP_NODE_ENTER: return "enter";
        case STEP_NODE_EXIT:  return "exit";
        case STEP_BRANCH_TAKEN: return "branch";
        case STEP_VAR_SET:    return "set_var";
        case STEP_ERROR:      return "error";
        case STEP_CONSEQUENCE: return "consequence";
        default:              return "unknown";
    }
}

static const char* step_color(StepType type) {
    switch (type) {
        case STEP_NODE_ENTER: return "#4D94FF"; // Blue
        case STEP_NODE_EXIT:  return "#66CC66"; // Green
        case STEP_BRANCH_TAKEN: return "#FFCC00"; // Yellow
        case STEP_VAR_SET:    return "#FF9933"; // Orange
        case STEP_ERROR:      return "#FF3333"; // Red
        case STEP_CONSEQUENCE: return "#9966CC"; // Purple
        default:              return "#CCCCCC"; // Gray
    }
}

static const char* step_shape(StepType type) {
    switch (type) {
        case STEP_NODE_ENTER: return "box";
        case STEP_NODE_EXIT:  return "box3d";
        case STEP_BRANCH_TAKEN: return "diamond";
        case STEP_VAR_SET:    return "note";
        case STEP_ERROR:      return "octagon";
        case STEP_CONSEQUENCE: return "component";
        default:              return "oval";
    }
}

static void write_trace_step_dot(FILE *file, TraceStep *step, TraceVizOptions *options) {
    if (!step || !file) return;

    // Generate step ID
    char step_id[64];
    snprintf(step_id, sizeof(step_id), "step_%zu", step->step_number);
    
    // Step label
    char label[512] = {0};
    snprintf(label, sizeof(label), "Step %zu: %s", step->step_number, step_type_name(step->type));
    
    // Add node info
    if (step->node) {
        strncat(label, "\\nNode: ", sizeof(label) - strlen(label) - 1);
        strncat(label, step->node->id, sizeof(label) - strlen(label) - 1);
    }
    
    // Add variable info
    if (step->var_name) {
        strncat(label, "\\nVar: ", sizeof(label) - strlen(label) - 1);
        strncat(label, step->var_name, sizeof(label) - strlen(label) - 1);
        
        if (options->show_values && step->var_value.type != VALUE_NULL) {
            char value_buf[128];
            reasons_value_format(&step->var_value, value_buf, sizeof(value_buf));
            strncat(label, " = ", sizeof(label) - strlen(label) - 1);
            strncat(label, value_buf, sizeof(label) - strlen(label) - 1);
        }
    }
    
    // Add explanation if available
    if (options->explainer && step->node) {
        const char *explanation = explain_get_step_explanation(options->explainer, step->step_number);
        if (explanation && *explanation) {
            strncat(label, "\\n\\n", sizeof(label) - strlen(label) - 1);
            
            // Truncate long explanations
            if (strlen(explanation) > 100) {
                char truncated[101];
                strncpy(truncated, explanation, 100);
                truncated[100] = '\0';
                strncat(label, truncated, sizeof(label) - strlen(label) - 1);
                strncat(label, "...", sizeof(label) - strlen(label) - 1);
            } else {
                strncat(label, explanation, sizeof(label) - strlen(label) - 1);
            }
        }
    }
    
    // Step attributes
    fprintf(file, "  \"%s\" [label=\"%s\" shape=%s fillcolor=\"%s\" style=filled ",
            step_id, label, step_shape(step->type), step_color(step->type));
    
    if (step->type == STEP_ERROR) {
        fprintf(file, "penwidth=3 color=\"#FF0000\" ");
    }
    
    fprintf(file, "];\n");
}

static void write_trace_edge_dot(FILE *file, TraceStep *from, TraceStep *to, TraceVizOptions *options) {
    if (!from || !to || !file) return;
    
    char from_id[64], to_id[64];
    snprintf(from_id, sizeof(from_id), "step_%zu", from->step_number);
    snprintf(to_id, sizeof(to_id), "step_%zu", to->step_number);
    
    fprintf(file, "  \"%s\" -> \"%s\"", from_id, to_id);
    
    // Time delta label
    if (options->show_performance && from->timestamp && to->timestamp) {
        double delta_ms = (to->timestamp - from->timestamp) * 1000.0;
        fprintf(file, " [label=\"%.3f ms\" fontsize=8]", delta_ms);
    }
    
    fprintf(file, ";\n");
}

static void generate_trace_dot_header(FILE *file, TraceVizOptions *options) {
    fprintf(file, "digraph ExecutionTrace {\n");
    fprintf(file, "  rankdir=LR;\n");
    fprintf(file, "  node [fontname=\"Helvetica\" fontsize=10];\n");
    fprintf(file, "  edge [fontname=\"Helvetica\" fontsize=8];\n\n");
    
    if (options->engine) {
        fprintf(file, "  layout=%s;\n", options->engine);
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

void trace_to_dot(trace_t *trace, const char *filename, TraceVizOptions *options) {
    if (!trace || vector_size(trace->steps) == 0) {
        LOG_ERROR("Invalid trace for DOT export");
        return;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        LOG_ERROR("Could not open file for writing: %s", filename);
        return;
    }
    
    // Default options if not provided
    TraceVizOptions default_options = {
        .show_state_diffs = false,
        .show_values = true,
        .show_performance = true,
        .colorize = true,
        .compact = false,
        .highlight_errors = true,
        .max_steps = 0,
        .engine = "dot",
        .trace = trace,
        .explainer = NULL
    };
    
    if (!options) options = &default_options;
    
    // Generate DOT file
    generate_trace_dot_header(file, options);
    
    // Write all steps
    for (size_t i = 0; i < vector_size(trace->steps); i++) {
        TraceStep *step = vector_at(trace->steps, i);
        if (options->max_steps > 0 && i >= options->max_steps) break;
        
        write_trace_step_dot(file, step, options);
    }
    
    // Write edges
    for (size_t i = 1; i < vector_size(trace->steps); i++) {
        TraceStep *prev = vector_at(trace->steps, i-1);
        TraceStep *current = vector_at(trace->steps, i);
        
        if (options->max_steps > 0 && i >= options->max_steps) break;
        
        write_trace_edge_dot(file, prev, current, options);
    }
    
    // Add legend
    fprintf(file, "\n  subgraph cluster_legend {\n");
    fprintf(file, "    label=\"Legend\";\n");
    fprintf(file, "    fontname=\"Helvetica-Bold\";\n");
    fprintf(file, "    rankdir=TB;\n");
    fprintf(file, "    style=filled;\n");
    fprintf(file, "    fillcolor=\"#F0F0F0\";\n");
    
    const struct {
        StepType type;
        const char *label;
    } legend_items[] = {
        {STEP_NODE_ENTER, "Node Enter"},
        {STEP_NODE_EXIT, "Node Exit"},
        {STEP_BRANCH_TAKEN, "Branch Taken"},
        {STEP_VAR_SET, "Variable Set"},
        {STEP_CONSEQUENCE, "Consequence"},
        {STEP_ERROR, "Error"}
    };
    
    for (size_t i = 0; i < sizeof(legend_items)/sizeof(legend_items[0]); i++) {
        fprintf(file, "    legend_%zu [label=\"%s\" shape=%s fillcolor=\"%s\" style=filled];\n",
                i, legend_items[i].label, 
                step_shape(legend_items[i].type),
                step_color(legend_items[i].type));
    }
    
    fprintf(file, "  }\n");
    
    fprintf(file, "}\n");
    fclose(file);
    
    LOG_INFO("Trace exported to DOT: %s", filename);
}

char* trace_to_dot_string(trace_t *trace, TraceVizOptions *options) {
    if (!trace || vector_size(trace->steps) == 0) return NULL;
    
    // Create memory buffer
    FILE *file = open_memstream(NULL, NULL);
    if (!file) return NULL;
    
    // Default options if not provided
    TraceVizOptions default_options = {
        .show_state_diffs = false,
        .show_values = true,
        .show_performance = true,
        .colorize = true,
        .compact = false,
        .highlight_errors = true,
        .max_steps = 0,
        .engine = "dot",
        .trace = trace,
        .explainer = NULL
    };
    
    if (!options) options = &default_options;
    
    // Generate DOT content
    generate_trace_dot_header(file, options);
    
    // Write all steps
    for (size_t i = 0; i < vector_size(trace->steps); i++) {
        TraceStep *step = vector_at(trace->steps, i);
        if (options->max_steps > 0 && i >= options->max_steps) break;
        
        write_trace_step_dot(file, step, options);
    }
    
    // Write edges
    for (size_t i = 1; i < vector_size(trace->steps); i++) {
        TraceStep *prev = vector_at(trace->steps, i-1);
        TraceStep *current = vector_at(trace->steps, i);
        
        if (options->max_steps > 0 && i >= options->max_steps) break;
        
        write_trace_edge_dot(file, prev, current, options);
    }
    
    fprintf(file, "}\n");
    
    // Get string from memory stream
    char *dot_string = NULL;
    fclose(file); // This writes to dot_string
    return dot_string;
}

void trace_to_timeline(trace_t *trace, FILE *output, TraceVizOptions *options) {
    if (!trace || !output) return;
    
    // Default options if not provided
    TraceVizOptions default_options = {
        .show_state_diffs = true,
        .show_values = true,
        .show_performance = true,
        .colorize = false,
        .compact = true,
        .highlight_errors = true,
        .max_steps = 50,
        .engine = NULL,
        .trace = trace,
        .explainer = NULL
    };
    
    if (!options) options = &default_options;
    
    fprintf(output, "Execution Timeline\n");
    fprintf(output, "==================\n\n");
    
    TraceStep *prev_step = NULL;
    size_t max_steps = options->max_steps > 0 ? 
        MIN(options->max_steps, vector_size(trace->steps)) : 
        vector_size(trace->steps);
    
    for (size_t i = 0; i < max_steps; i++) {
        TraceStep *step = vector_at(trace->steps, i);
        
        // Step header
        fprintf(output, "Step %zu: %s\n", step->step_number, step_type_name(step->type));
        
        // Timestamp
        if (step->timestamp) {
            struct tm *tm = localtime(&step->timestamp);
            char time_buf[32];
            strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm);
            fprintf(output, "  Time: %s", time_buf);
            
            // Time delta
            if (prev_step && options->show_performance) {
                double delta_ms = (step->timestamp - prev_step->timestamp) * 1000.0;
                fprintf(output, " (+%.3f ms)", delta_ms);
            }
            fprintf(output, "\n");
        }
        
        // Node info
        if (step->node) {
            fprintf(output, "  Node: %s", step->node->id);
            if (step->node->description) {
                fprintf(output, " (%s)", step->node->description);
            }
            fprintf(output, "\n");
        }
        
        // Variable info
        if (step->var_name) {
            fprintf(output, "  Variable: %s", step->var_name);
            if (options->show_values && step->var_value.type != VALUE_NULL) {
                fprintf(output, " = ");
                reasons_value_print(&step->var_value, output);
            }
            fprintf(output, "\n");
        }
        
        // Error info
        if (step->error_code != ERROR_NONE) {
            fprintf(output, "  Error: %s (%d) - %s\n", 
                    error_message(step->error_code),
                    step->error_code,
                    step->error_message ? step->error_message : "");
        }
        
        // State diff
        if (options->show_state_diffs && prev_step && step->state_diff) {
            // Simplified diff display
            fprintf(output, "  State Changes:\n");
            for (size_t j = 0; j < vector_size(step->state_diff->changes); j++) {
                StateChange *change = vector_at(step->state_diff->changes, j);
                fprintf(output, "    %s: ", change->var_name);
                reasons_value_print(&change->old_value, output);
                fprintf(output, " => ");
                reasons_value_print(&change->new_value, output);
                fprintf(output, "\n");
            }
        }
        
        // Explanation
        if (options->explainer) {
            const char *explanation = explain_get_step_explanation(options->explainer, step->step_number);
            if (explanation && *explanation) {
                fprintf(output, "  Explanation: %s\n", explanation);
            }
        }
        
        fprintf(output, "\n");
        prev_step = step;
    }
}

void trace_export_png(trace_t *trace, const char *filename, TraceVizOptions *options) {
    char dot_file[PATH_MAX];
    snprintf(dot_file, sizeof(dot_file), "%s.dot", filename);
    
    // Generate DOT file
    trace_to_dot(trace, dot_file, options);
    
    // Convert to PNG using Graphviz
    char command[PATH_MAX * 2];
    const char *engine = options && options->engine ? options->engine : "dot";
    snprintf(command, sizeof(command), "%s -Tpng %s -o %s", engine, dot_file, filename);
    
    int result = system(command);
    if (result != 0) {
        LOG_ERROR("Failed to generate PNG: %s", command);
    } else {
        LOG_INFO("Trace exported to PNG: %s", filename);
    }
    
    // Remove temporary DOT file
    remove(dot_file);
}

void trace_export_svg(trace_t *trace, const char *filename, TraceVizOptions *options) {
    char dot_file[PATH_MAX];
    snprintf(dot_file, sizeof(dot_file), "%s.dot", filename);
    
    // Generate DOT file
    trace_to_dot(trace, dot_file, options);
    
    // Convert to SVG
    char command[PATH_MAX * 2];
    const char *engine = options && options->engine ? options->engine : "dot";
    snprintf(command, sizeof(command), "%s -Tsvg %s -o %s", engine, dot_file, filename);
    
    int result = system(command);
    if (result != 0) {
        LOG_ERROR("Failed to generate SVG: %s", command);
    } else {
        LOG_INFO("Trace exported to SVG: %s", filename);
    }
    
    // Remove temporary DOT file
    remove(dot_file);
}

TraceVizOptions* trace_viz_options_create() {
    TraceVizOptions *options = mem_alloc(sizeof(TraceVizOptions));
    if (options) {
        memset(options, 0, sizeof(TraceVizOptions));
        options->show_state_diffs = false;
        options->show_values = true;
        options->show_performance = true;
        options->colorize = true;
        options->compact = false;
        options->highlight_errors = true;
        options->max_steps = 0;
        options->engine = "dot";
    }
    return options;
}

void trace_viz_options_destroy(TraceVizOptions *options) {
    if (options) {
        mem_free(options);
    }
}

void trace_viz_set_explainer(TraceVizOptions *options, explain_engine_t *explainer) {
    if (options) options->explainer = explainer;
}

void trace_viz_set_show_state_diffs(TraceVizOptions *options, bool show) {
    if (options) options->show_state_diffs = show;
}

void trace_viz_set_show_values(TraceVizOptions *options, bool show) {
    if (options) options->show_values = show;
}

void trace_viz_set_show_performance(TraceVizOptions *options, bool show) {
    if (options) options->show_performance = show;
}

void trace_viz_set_colorize(TraceVizOptions *options, bool colorize) {
    if (options) options->colorize = colorize;
}

void trace_viz_set_compact(TraceVizOptions *options, bool compact) {
    if (options) options->compact = compact;
}

void trace_viz_set_highlight_errors(TraceVizOptions *options, bool highlight) {
    if (options) options->highlight_errors = highlight;
}

void trace_viz_set_max_steps(TraceVizOptions *options, unsigned max_steps) {
    if (options) options->max_steps = max_steps;
}

void trace_viz_set_engine(TraceVizOptions *options, const char *engine) {
    if (options) options->engine = engine;
}

void trace_replay(trace_t *trace, FILE *output, TraceVizOptions *options) {
    if (!trace || !output) return;
    
    // Default options if not provided
    TraceVizOptions default_options = {
        .show_state_diffs = true,
        .show_values = true,
        .show_performance = true,
        .colorize = false,
        .compact = true,
        .highlight_errors = true,
        .max_steps = 0,
        .engine = NULL,
        .trace = trace,
        .explainer = NULL
    };
    
    if (!options) options = &default_options;
    
    fprintf(output, "Trace Replay\n");
    fprintf(output, "============\n\n");
    
    size_t max_steps = options->max_steps > 0 ? 
        MIN(options->max_steps, vector_size(trace->steps)) : 
        vector_size(trace->steps);
    
    for (size_t i = 0; i < max_steps; i++) {
        TraceStep *step = vector_at(trace->steps, i);
        
        fprintf(output, "[Step %zu/%zu] ", i+1, max_steps);
        fprintf(output, "Type: %s\n", step_type_name(step->type));
        
        if (step->node) {
            fprintf(output, "  Node: %s\n", step->node->id);
            if (step->node->description) {
                fprintf(output, "  Description: %s\n", step->node->description);
            }
        }
        
        if (step->var_name) {
            fprintf(output, "  Variable: %s = ", step->var_name);
            reasons_value_print(&step->var_value, output);
            fprintf(output, "\n");
        }
        
        if (step->error_code != ERROR_NONE) {
            fprintf(output, "  ERROR: %s (%d)\n", 
                    error_message(step->error_code), step->error_code);
            if (step->error_message) {
                fprintf(output, "  Message: %s\n", step->error_message);
            }
        }
        
        if (options->explainer) {
            const char *explanation = explain_get_step_explanation(options->explainer, step->step_number);
            if (explanation && *explanation) {
                fprintf(output, "  Explanation: %s\n", explanation);
            }
        }
        
        fprintf(output, "Press Enter to continue...");
        fflush(output);
        getchar();
        
        fprintf(output, "\n\033[2J\033[1;1H"); // Clear screen
    }
    
    fprintf(output, "Trace replay complete.\n");
}
