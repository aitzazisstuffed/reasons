/*
 * graph.c - Graphviz DOT generation utilities for Reasons DSL
 *
 * Features:
 * - Reusable DOT generation core
 * - Support for nodes, edges, and subgraphs
 * - Attribute management (styles, colors, labels)
 * - String-based and file-based output
 * - Memory-safe operations
 * - Error handling
 * - Consistent styling across visualizations
 */

#include "reasons/viz.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    char *key;
    char *value;
} GraphAttribute;

typedef struct GraphNode {
    char *id;
    vector_t *attributes;  // Vector of GraphAttribute
} GraphNode;

typedef struct GraphEdge {
    char *from_id;
    char *to_id;
    vector_t *attributes;  // Vector of GraphAttribute
} GraphEdge;

typedef struct GraphContext {
    char *name;
    char *engine;
    char *rankdir;
    vector_t *global_attributes;  // Graph-level attributes
    vector_t *default_node_attrs; // Default node attributes
    vector_t *default_edge_attrs; // Default edge attributes
    vector_t *nodes;        // Vector of GraphNode*
    vector_t *edges;        // Vector of GraphEdge*
    vector_t *subgraphs;    // Vector of GraphContext* (for clusters)
} GraphContext;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static GraphAttribute* create_attribute(const char *key, const char *value) {
    GraphAttribute *attr = mem_alloc(sizeof(GraphAttribute));
    if (!attr) return NULL;
    
    attr->key = key ? strdup(key) : NULL;
    attr->value = value ? strdup(value) : NULL;
    
    if ((key && !attr->key) || (value && !attr->value)) {
        if (attr->key) mem_free(attr->key);
        if (attr->value) mem_free(attr->value);
        mem_free(attr);
        return NULL;
    }
    
    return attr;
}

static void free_attribute(void *attr) {
    if (!attr) return;
    GraphAttribute *a = (GraphAttribute*)attr;
    if (a->key) mem_free(a->key);
    if (a->value) mem_free(a->value);
    mem_free(a);
}

static void write_attributes(FILE *file, vector_t *attributes) {
    if (!attributes || vector_size(attributes) == 0) return;
    
    fprintf(file, " [");
    for (size_t i = 0; i < vector_size(attributes); i++) {
        GraphAttribute *attr = vector_at(attributes, i);
        if (i > 0) fprintf(file, " ");
        fprintf(file, "%s=\"%s\"", attr->key, attr->value);
    }
    fprintf(file, "]");
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

GraphContext* graph_new_context(const char *name) {
    GraphContext *ctx = mem_alloc(sizeof(GraphContext));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(GraphContext));
    ctx->name = name ? strdup(name) : strdup("DecisionGraph");
    ctx->engine = strdup("dot");
    ctx->rankdir = strdup("TB");
    
    ctx->global_attributes = vector_create(8);
    ctx->default_node_attrs = vector_create(8);
    ctx->default_edge_attrs = vector_create(8);
    ctx->nodes = vector_create(32);
    ctx->edges = vector_create(64);
    ctx->subgraphs = vector_create(4);
    
    // Set default styles
    graph_add_global_attr(ctx, "fontname", "Helvetica");
    graph_add_default_node_attr(ctx, "fontname", "Helvetica");
    graph_add_default_node_attr(ctx, "fontsize", "10");
    graph_add_default_node_attr(ctx, "shape", "box");
    graph_add_default_node_attr(ctx, "style", "filled");
    graph_add_default_edge_attr(ctx, "fontname", "Helvetica");
    graph_add_default_edge_attr(ctx, "fontsize", "9");
    
    return ctx;
}

void graph_free_context(GraphContext *ctx) {
    if (!ctx) return;
    
    if (ctx->name) mem_free(ctx->name);
    if (ctx->engine) mem_free(ctx->engine);
    if (ctx->rankdir) mem_free(ctx->rankdir);
    
    vector_destroy_deep(ctx->global_attributes, free_attribute);
    vector_destroy_deep(ctx->default_node_attrs, free_attribute);
    vector_destroy_deep(ctx->default_edge_attrs, free_attribute);
    
    // Free nodes
    for (size_t i = 0; i < vector_size(ctx->nodes); i++) {
        GraphNode *node = vector_at(ctx->nodes, i);
        if (node->id) mem_free(node->id);
        vector_destroy_deep(node->attributes, free_attribute);
        mem_free(node);
    }
    vector_destroy(ctx->nodes);
    
    // Free edges
    for (size_t i = 0; i < vector_size(ctx->edges); i++) {
        GraphEdge *edge = vector_at(ctx->edges, i);
        if (edge->from_id) mem_free(edge->from_id);
        if (edge->to_id) mem_free(edge->to_id);
        vector_destroy_deep(edge->attributes, free_attribute);
        mem_free(edge);
    }
    vector_destroy(ctx->edges);
    
    // Free subgraphs
    for (size_t i = 0; i < vector_size(ctx->subgraphs); i++) {
        graph_free_context(vector_at(ctx->subgraphs, i));
    }
    vector_destroy(ctx->subgraphs);
    
    mem_free(ctx);
}

void graph_set_engine(GraphContext *ctx, const char *engine) {
    if (!ctx || !engine) return;
    if (ctx->engine) mem_free(ctx->engine);
    ctx->engine = strdup(engine);
}

void graph_set_rankdir(GraphContext *ctx, const char *rankdir) {
    if (!ctx || !rankdir) return;
    if (ctx->rankdir) mem_free(ctx->rankdir);
    ctx->rankdir = strdup(rankdir);
}

void graph_add_global_attr(GraphContext *ctx, const char *key, const char *value) {
    if (!ctx || !key || !value) return;
    GraphAttribute *attr = create_attribute(key, value);
    if (attr) vector_append(ctx->global_attributes, attr);
}

void graph_add_default_node_attr(GraphContext *ctx, const char *key, const char *value) {
    if (!ctx || !key || !value) return;
    GraphAttribute *attr = create_attribute(key, value);
    if (attr) vector_append(ctx->default_node_attrs, attr);
}

void graph_add_default_edge_attr(GraphContext *ctx, const char *key, const char *value) {
    if (!ctx || !key || !value) return;
    GraphAttribute *attr = create_attribute(key, value);
    if (attr) vector_append(ctx->default_edge_attrs, attr);
}

GraphNode* graph_add_node(GraphContext *ctx, const char *id) {
    if (!ctx || !id) return NULL;
    
    // Check if node already exists
    for (size_t i = 0; i < vector_size(ctx->nodes); i++) {
        GraphNode *node = vector_at(ctx->nodes, i);
        if (strcmp(node->id, id) == 0) return node;
    }
    
    GraphNode *node = mem_alloc(sizeof(GraphNode));
    if (!node) return NULL;
    
    node->id = strdup(id);
    node->attributes = vector_create(4);
    
    if (!node->id || !node->attributes) {
        if (node->id) mem_free(node->id);
        if (node->attributes) vector_destroy(node->attributes);
        mem_free(node);
        return NULL;
    }
    
    vector_append(ctx->nodes, node);
    return node;
}

void graph_node_set_attr(GraphNode *node, const char *key, const char *value) {
    if (!node || !key || !value) return;
    GraphAttribute *attr = create_attribute(key, value);
    if (attr) vector_append(node->attributes, attr);
}

GraphEdge* graph_add_edge(GraphContext *ctx, const char *from_id, const char *to_id) {
    if (!ctx || !from_id || !to_id) return NULL;
    
    GraphEdge *edge = mem_alloc(sizeof(GraphEdge));
    if (!edge) return NULL;
    
    edge->from_id = strdup(from_id);
    edge->to_id = strdup(to_id);
    edge->attributes = vector_create(2);
    
    if (!edge->from_id || !edge->to_id || !edge->attributes) {
        if (edge->from_id) mem_free(edge->from_id);
        if (edge->to_id) mem_free(edge->to_id);
        if (edge->attributes) vector_destroy(edge->attributes);
        mem_free(edge);
        return NULL;
    }
    
    vector_append(ctx->edges, edge);
    return edge;
}

void graph_edge_set_attr(GraphEdge *edge, const char *key, const char *value) {
    if (!edge || !key || !value) return;
    GraphAttribute *attr = create_attribute(key, value);
    if (attr) vector_append(edge->attributes, attr);
}

GraphContext* graph_add_subgraph(GraphContext *parent, const char *name) {
    if (!parent || !name) return NULL;
    
    GraphContext *sub = graph_new_context(name);
    if (sub) vector_append(parent->subgraphs, sub);
    return sub;
}

static void graph_render_dot(FILE *file, GraphContext *ctx, int indent_level) {
    if (!file || !ctx) return;
    
    // Indentation
    const char *indent = "  ";
    for (int i = 0; i < indent_level; i++) fputs(indent, file);
    
    // Graph header
    if (indent_level == 0) {
        fprintf(file, "digraph %s {\n", ctx->name);
    } else {
        fprintf(file, "subgraph cluster_%s {\n", ctx->name);
        fprintf(file, "%slabel=\"%s\";\n", indent, ctx->name);
    }
    
    // Apply global attributes
    if (ctx->engine) {
        for (int i = 0; i < indent_level + 1; i++) fputs(indent, file);
        fprintf(file, "layout=%s;\n", ctx->engine);
    }
    
    if (ctx->rankdir) {
        for (int i = 0; i < indent_level + 1; i++) fputs(indent, file);
        fprintf(file, "rankdir=%s;\n", ctx->rankdir);
    }
    
    // Global attributes
    for (size_t i = 0; i < vector_size(ctx->global_attributes); i++) {
        GraphAttribute *attr = vector_at(ctx->global_attributes, i);
        for (int j = 0; j < indent_level + 1; j++) fputs(indent, file);
        fprintf(file, "%s=\"%s\";\n", attr->key, attr->value);
    }
    
    // Default node attributes
    if (vector_size(ctx->default_node_attrs) > 0) {
        for (int i = 0; i < indent_level + 1; i++) fputs(indent, file);
        fprintf(file, "node ");
        write_attributes(file, ctx->default_node_attrs);
        fprintf(file, ";\n");
    }
    
    // Default edge attributes
    if (vector_size(ctx->default_edge_attrs) > 0) {
        for (int i = 0; i < indent_level + 1; i++) fputs(indent, file);
        fprintf(file, "edge ");
        write_attributes(file, ctx->default_edge_attrs);
        fprintf(file, ";\n");
    }
    
    // Render subgraphs
    for (size_t i = 0; i < vector_size(ctx->subgraphs); i++) {
        graph_render_dot(file, vector_at(ctx->subgraphs, i), indent_level + 1);
    }
    
    // Render nodes
    for (size_t i = 0; i < vector_size(ctx->nodes); i++) {
        GraphNode *node = vector_at(ctx->nodes, i);
        for (int j = 0; j < indent_level + 1; j++) fputs(indent, file);
        fprintf(file, "\"%s\"", node->id);
        write_attributes(file, node->attributes);
        fprintf(file, ";\n");
    }
    
    // Render edges
    for (size_t i = 0; i < vector_size(ctx->edges); i++) {
        GraphEdge *edge = vector_at(ctx->edges, i);
        for (int j = 0; j < indent_level + 1; j++) fputs(indent, file);
        fprintf(file, "\"%s\" -> \"%s\"", edge->from_id, edge->to_id);
        write_attributes(file, edge->attributes);
        fprintf(file, ";\n");
    }
    
    // Close graph
    for (int i = 0; i < indent_level; i++) fputs(indent, file);
    fprintf(file, "}\n");
}

void graph_render_to_file(GraphContext *ctx, const char *filename) {
    if (!ctx || !filename) return;
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        LOG_ERROR("Could not open file for writing: %s", filename);
        return;
    }
    
    graph_render_dot(file, ctx, 0);
    fclose(file);
    LOG_INFO("Graph rendered to DOT: %s", filename);
}

char* graph_render_to_string(GraphContext *ctx) {
    if (!ctx) return NULL;
    
    FILE *file = open_memstream(NULL, NULL);
    if (!file) return NULL;
    
    graph_render_dot(file, ctx, 0);
    fclose(file); // This writes to the internal buffer
    
    char *dot_string = NULL;
    size_t size;
    FILE *tmp = open_memstream(&dot_string, &size);
    if (tmp) {
        graph_render_dot(tmp, ctx, 0);
        fclose(tmp);
    }
    
    return dot_string;
}
