#ifndef REASONS_VIZ_H
#define REASONS_VIZ_H

#include <stdio.h>
#include <stdbool.h>

/* Opaque context structure */
typedef struct GraphContext GraphContext;

/* Opaque node structure */
typedef struct GraphNode GraphNode;

/* Opaque edge structure */
typedef struct GraphEdge GraphEdge;

/* ======== PUBLIC API ======== */

/**
 * Creates a new graph visualization context
 * 
 * @param name Name for the graph (or NULL for default)
 * @return New graph context or NULL on failure
 */
GraphContext* graph_new_context(const char *name);

/**
 * Frees a graph context and all associated resources
 * 
 * @param ctx Graph context to destroy
 */
void graph_free_context(GraphContext *ctx);

/**
 * Sets the layout engine for the graph
 * 
 * @param ctx Graph context
 * @param engine Layout engine (e.g., "dot", "neato", "fdp")
 */
void graph_set_engine(GraphContext *ctx, const char *engine);

/**
 * Sets the rank direction for the graph
 * 
 * @param ctx Graph context
 * @param rankdir Rank direction ("TB", "LR", "BT", "RL")
 */
void graph_set_rankdir(GraphContext *ctx, const char *rankdir);

/**
 * Adds a global graph attribute
 * 
 * @param ctx Graph context
 * @param key Attribute key
 * @param value Attribute value
 */
void graph_add_global_attr(GraphContext *ctx, const char *key, const char *value);

/**
 * Adds a default node attribute
 * 
 * @param ctx Graph context
 * @param key Attribute key
 * @param value Attribute value
 */
void graph_add_default_node_attr(GraphContext *ctx, const char *key, const char *value);

/**
 * Adds a default edge attribute
 * 
 * @param ctx Graph context
 * @param key Attribute key
 * @param value Attribute value
 */
void graph_add_default_edge_attr(GraphContext *ctx, const char *key, const char *value);

/**
 * Adds a node to the graph
 * 
 * @param ctx Graph context
 * @param id Unique node identifier
 * @return Created node or NULL on failure
 */
GraphNode* graph_add_node(GraphContext *ctx, const char *id);

/**
 * Sets an attribute for a node
 * 
 * @param node Target node
 * @param key Attribute key
 * @param value Attribute value
 */
void graph_node_set_attr(GraphNode *node, const char *key, const char *value);

/**
 * Adds an edge between two nodes
 * 
 * @param ctx Graph context
 * @param from_id Source node ID
 * @param to_id Target node ID
 * @return Created edge or NULL on failure
 */
GraphEdge* graph_add_edge(GraphContext *ctx, const char *from_id, const char *to_id);

/**
 * Sets an attribute for an edge
 * 
 * @param edge Target edge
 * @param key Attribute key
 * @param value Attribute value
 */
void graph_edge_set_attr(GraphEdge *edge, const char *key, const char *value);

/**
 * Adds a subgraph to the current graph
 * 
 * @param parent Parent graph context
 * @param name Subgraph name
 * @return New subgraph context or NULL on failure
 */
GraphContext* graph_add_subgraph(GraphContext *parent, const char *name);

/**
 * Renders the graph to a DOT file
 * 
 * @param ctx Graph context
 * @param filename Output filename
 */
void graph_render_to_file(GraphContext *ctx, const char *filename);

/**
 * Renders the graph to a DOT format string
 * 
 * @param ctx Graph context
 * @return DOT format string (caller must free) or NULL on failure
 */
char* graph_render_to_string(GraphContext *ctx);

#endif /* REASONS_VIZ_H */
