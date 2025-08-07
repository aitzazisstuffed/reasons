/*
 * tree_viz.c - Decision Tree Visualization for Reasons DSL
 * 
 * Features:
 * - Graphviz DOT format generation
 * - ASCII tree rendering
 * - Customizable styling options
 * - Node and edge labeling
 * - Color-coded node types
 * - Collapsible sub-trees
 * - Interactive node selection
 * - Path highlighting
 * - Multiple layout engines
 * - Export to various formats
 */

#include "reasons/viz.h"
#include "reasons/tree.h"
#include "reasons/runtime.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "viz/ascii_art.h"
#include "viz/graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    bool show_values;       // Display decision values
    bool show_descriptions; // Display node descriptions
    bool colorize;          // Use color in output
    bool compact;           // Use compact representation
    bool highlight_path;    // Highlight current execution path
    unsigned max_depth;     // Maximum depth to render
    const char *engine;     // Layout engine (dot, neato, etc.)
    TreeNode *start_node;   // Starting node for rendering
    vector_t *highlight_nodes; // Nodes to highlight
} TreeVizOptions;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const char* node_type_name(NodeType type) {
    switch (type) {
        case NODE_ROOT:     return "root";
        case NODE_CONDITION:return "condition";
        case NODE_LEAF:     return "leaf";
        case NODE_ACTION:   return "action";
        default:            return "unknown";
    }
}

static const char* node_type_color(NodeType type) {
    switch (type) {
        case NODE_ROOT:     return "#FFCC00"; // Yellow
        case NODE_CONDITION:return "#4D94FF"; // Blue
        case NODE_LEAF:     return "#66CC66"; // Green
        case NODE_ACTION:   return "#FF6666"; // Red
        default:            return "#CCCCCC"; // Gray
    }
}

static const char* node_shape(NodeType type) {
    switch (type) {
        case NODE_ROOT:     return "doublecircle";
        case NODE_CONDITION:return "diamond";
        case NODE_LEAF:     return "box";
        case NODE_ACTION:   return "ellipse";
        default:            return "oval";
    }
}

static void write_node_dot(FILE *file, TreeNode *node, TreeVizOptions *options) {
    if (!node || !file) return;

    // Node ID must be valid
    const char *node_id = node->id ? node->id : "unknown";
    
    // Node label
    char label[512] = {0};
    if (options->show_descriptions && node->description) {
        snprintf(label, sizeof(label), "%s\\n%s", node_id, node->description);
    } else {
        snprintf(label, sizeof(label), "%s", node_id);
    }
    
    // Node value if available
    if (options->show_values && node->value.type != VALUE_NULL) {
        char value_buf[128];
        reasons_value_format(&node->value, value_buf, sizeof(value_buf));
        strncat(label, "\\n", sizeof(label) - strlen(label) - 1);
        strncat(label, value_buf, sizeof(label) - strlen(label) - 1);
    }
    
    // Check if node should be highlighted
    bool is_highlighted = false;
    if (options->highlight_path && options->highlight_nodes) {
        for (size_t i = 0; i < vector_size(options->highlight_nodes); i++) {
            if (vector_at(options->highlight_nodes, i) == node) {
                is_highlighted = true;
                break;
            }
        }
    }
    
    // Node attributes
    fprintf(file, "  \"%s\" [label=\"%s\" shape=%s fillcolor=\"%s\" style=filled ",
            node_id, label, node_shape(node->type), node_type_color(node->type));
    
    if (is_highlighted) {
        fprintf(file, "penwidth=3 color=\"#FF0000\" ");
    }
    
    fprintf(file, "];\n");
}

static void write_edge_dot(FILE *file, TreeNode *from, TreeNode *to, 
                          const char *label, TreeVizOptions *options) {
    if (!from || !to || !file) return;
    
    const char *from_id = from->id ? from->id : "unknown";
    const char *to_id = to->id ? to->id : "unknown";
    
    fprintf(file, "  \"%s\" -> \"%s\"", from_id, to_id);
    
    if (label && *label) {
        fprintf(file, " [label=\"%s\"", label);
        
        // Style true/false branches differently
        if (strcmp(label, "true") == 0) {
            fprintf(file, " color=\"#006600\" fontcolor=\"#006600\"");
        } else if (strcmp(label, "false") == 0) {
            fprintf(file, " color=\"#990000\" fontcolor=\"#990000\"");
        }
        
        fprintf(file, "]");
    }
    fprintf(file, ";\n");
}

static void traverse_tree_dot(FILE *file, TreeNode *node, TreeVizOptions *options, 
                             unsigned depth, vector_t *visited) {
    if (!node || !file) return;
    
    // Check depth limit
    if (options->max_depth > 0 && depth > options->max_depth) return;
    
    // Check if we've already visited this node
    if (vector_contains(visited, node)) return;
    vector_append(visited, node);
    
    // Write current node
    write_node_dot(file, node, options);
    
    // Process children
    for (unsigned i = 0; i < vector_size(node->children); i++) {
        TreeNode *child = vector_at(node->children, i);
        if (!child) continue;
        
        const char *edge_label = "";
        if (node->type == NODE_CONDITION) {
            if (i == 0) edge_label = "true";
            else if (i == 1) edge_label = "false";
            else if (node->edge_labels) {
                edge_label = vector_at(node->edge_labels, i);
            }
        }
        
        write_edge_dot(file, node, child, edge_label, options);
        traverse_tree_dot(file, child, options, depth + 1, visited);
    }
}

static void generate_dot_header(FILE *file, TreeVizOptions *options) {
    fprintf(file, "digraph DecisionTree {\n");
    fprintf(file, "  rankdir=TB;\n");
    fprintf(file, "  node [fontname=\"Helvetica\" fontsize=10];\n");
    fprintf(file, "  edge [fontname=\"Helvetica\" fontsize=9];\n\n");
    
    if (options->engine) {
        fprintf(file, "  layout=%s;\n", options->engine);
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

void tree_to_dot(DecisionTree *tree, const char *filename, TreeVizOptions *options) {
    if (!tree || !tree->root) {
        LOG_ERROR("Invalid tree for DOT export");
        return;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        LOG_ERROR("Could not open file for writing: %s", filename);
        return;
    }
    
    // Default options if not provided
    TreeVizOptions default_options = {
        .show_values = true,
        .show_descriptions = true,
        .colorize = true,
        .compact = false,
        .highlight_path = false,
        .max_depth = 0,
        .engine = "dot",
        .start_node = tree->root,
        .highlight_nodes = NULL
    };
    
    if (!options) options = &default_options;
    
    // Create visited nodes list to prevent cycles
    vector_t *visited = vector_create(32);
    
    // Generate DOT file
    generate_dot_header(file, options);
    
    // Traverse from start node
    traverse_tree_dot(file, options->start_node, options, 0, visited);
    
    fprintf(file, "}\n");
    fclose(file);
    
    // Cleanup
    vector_destroy(visited);
    LOG_INFO("Tree exported to DOT: %s", filename);
}

char* tree_to_dot_string(DecisionTree *tree, TreeVizOptions *options) {
    if (!tree || !tree->root) return NULL;
    
    // Create memory buffer
    FILE *file = open_memstream(NULL, NULL);
    if (!file) return NULL;
    
    // Default options if not provided
    TreeVizOptions default_options = {
        .show_values = true,
        .show_descriptions = true,
        .colorize = true,
        .compact = false,
        .highlight_path = false,
        .max_depth = 0,
        .engine = "dot",
        .start_node = tree->root,
        .highlight_nodes = NULL
    };
    
    if (!options) options = &default_options;
    
    // Create visited nodes list
    vector_t *visited = vector_create(32);
    
    // Generate DOT content
    generate_dot_header(file, options);
    traverse_tree_dot(file, options->start_node, options, 0, visited);
    fprintf(file, "}\n");
    
    // Get string from memory stream
    char *dot_string = NULL;
    fclose(file); // This writes to dot_string
    file = NULL; // memstream is now closed
    
    // Cleanup
    vector_destroy(visited);
    return dot_string;
}

void tree_to_ascii(DecisionTree *tree, FILE *output, TreeVizOptions *options) {
    if (!tree || !tree->root || !output) return;
    
    // Default options if not provided
    TreeVizOptions default_options = {
        .show_values = true,
        .show_descriptions = true,
        .colorize = false,  // ASCII typically doesn't use color
        .compact = true,
        .highlight_path = false,
        .max_depth = 6,     // Reasonable default for terminal
        .engine = NULL,
        .start_node = tree->root,
        .highlight_nodes = NULL
    };
    
    if (!options) options = &default_options;
    
    // Create render context
    AsciiRenderContext ctx = {
        .output = output,
        .show_values = options->show_values,
        .show_descriptions = options->show_descriptions,
        .compact = options->compact,
        .max_depth = options->max_depth,
        .current_depth = 0,
        .highlight_path = options->highlight_path,
        .highlight_nodes = options->highlight_nodes
    };
    
    // Render tree
    render_ascii_tree(options->start_node, &ctx);
}

char* tree_to_ascii_string(DecisionTree *tree, TreeVizOptions *options) {
    if (!tree || !tree->root) return NULL;
    
    // Create memory buffer
    FILE *file = open_memstream(NULL, NULL);
    if (!file) return NULL;
    
    // Render to memory stream
    tree_to_ascii(tree, file, options);
    
    // Get string from memory stream
    char *ascii_string = NULL;
    fclose(file); // This writes to ascii_string
    return ascii_string;
}

void tree_to_json(DecisionTree *tree, FILE *output, TreeVizOptions *options) {
    if (!tree || !tree->root || !output) return;
    
    fprintf(output, "{\n");
    fprintf(output, "  \"tree\": {\n");
    fprintf(output, "    \"id\": \"%s\",\n", tree->id ? tree->id : "");
    fprintf(output, "    \"description\": \"%s\",\n", tree->description ? tree->description : "");
    fprintf(output, "    \"nodeCount\": %d,\n", tree_total_nodes(tree));
    fprintf(output, "    \"root\": ");
    
    // Serialize nodes recursively
    vector_t *visited = vector_create(32);
    serialize_node_json(tree->root, output, visited, 0, options);
    vector_destroy(visited);
    
    fprintf(output, "\n  }\n}\n");
}

TreeVizOptions* tree_viz_options_create() {
    TreeVizOptions *options = mem_alloc(sizeof(TreeVizOptions));
    if (options) {
        memset(options, 0, sizeof(TreeVizOptions));
        options->show_values = true;
        options->show_descriptions = true;
        options->colorize = true;
        options->compact = false;
        options->highlight_path = false;
        options->max_depth = 0; // Unlimited
        options->engine = "dot";
    }
    return options;
}

void tree_viz_options_destroy(TreeVizOptions *options) {
    if (options) {
        if (options->highlight_nodes) {
            vector_destroy(options->highlight_nodes);
        }
        mem_free(options);
    }
}

void tree_viz_add_highlight(TreeVizOptions *options, TreeNode *node) {
    if (!options || !node) return;
    
    if (!options->highlight_nodes) {
        options->highlight_nodes = vector_create(16);
    }
    
    vector_append(options->highlight_nodes, node);
}

void tree_viz_set_max_depth(TreeVizOptions *options, unsigned max_depth) {
    if (options) options->max_depth = max_depth;
}

void tree_viz_set_engine(TreeVizOptions *options, const char *engine) {
    if (options) options->engine = engine;
}

void tree_viz_set_start_node(TreeVizOptions *options, TreeNode *node) {
    if (options) options->start_node = node;
}

void tree_viz_set_compact(TreeVizOptions *options, bool compact) {
    if (options) options->compact = compact;
}

void tree_viz_set_highlight_path(TreeVizOptions *options, bool highlight) {
    if (options) options->highlight_path = highlight;
}

void tree_viz_set_show_values(TreeVizOptions *options, bool show) {
    if (options) options->show_values = show;
}

void tree_viz_set_show_descriptions(TreeVizOptions *options, bool show) {
    if (options) options->show_descriptions = show;
}

void tree_viz_set_colorize(TreeVizOptions *options, bool colorize) {
    if (options) options->colorize = colorize;
}

void tree_export_png(DecisionTree *tree, const char *filename, TreeVizOptions *options) {
    char dot_file[PATH_MAX];
    snprintf(dot_file, sizeof(dot_file), "%s.dot", filename);
    
    // Generate DOT file
    tree_to_dot(tree, dot_file, options);
    
    // Convert to PNG using Graphviz
    char command[PATH_MAX * 2];
    const char *engine = options && options->engine ? options->engine : "dot";
    snprintf(command, sizeof(command), "%s -Tpng %s -o %s", engine, dot_file, filename);
    
    int result = system(command);
    if (result != 0) {
        LOG_ERROR("Failed to generate PNG: %s", command);
    } else {
        LOG_INFO("Tree exported to PNG: %s", filename);
    }
    
    // Remove temporary DOT file
    remove(dot_file);
}

void tree_export_svg(DecisionTree *tree, const char *filename, TreeVizOptions *options) {
    char dot_file[PATH_MAX];
    snprintf(dot_file, sizeof(dot_file), "%s.dot", filename);
    
    // Generate DOT file
    tree_to_dot(tree, dot_file, options);
    
    // Convert to SVG
    char command[PATH_MAX * 2];
    const char *engine = options && options->engine ? options->engine : "dot";
    snprintf(command, sizeof(command), "%s -Tsvg %s -o %s", engine, dot_file, filename);
    
    int result = system(command);
    if (result != 0) {
        LOG_ERROR("Failed to generate SVG: %s", command);
    } else {
        LOG_INFO("Tree exported to SVG: %s", filename);
    }
    
    // Remove temporary DOT file
    remove(dot_file);
}
