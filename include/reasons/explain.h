#ifndef REASONS_EXPLAIN_H
#define REASONS_EXPLAIN_H

#include "reasons/ast.h"
#include "reasons/trace.h"
#include "reasons/types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Explanation modes */
typedef enum {
    EXPLAIN_WHY,        /* Explain why something happened */
    EXPLAIN_WHY_NOT,    /* Explain why something didn't happen */
    EXPLAIN_FULL        /* Full detailed explanation */
} explain_mode_t;

/* Explanation statistics */
typedef struct {
    size_t decisions_explained;      /* Decisions explained */
    size_t conditions_explained;     /* Conditions explained */
    size_t consequences_explained;   /* Consequences explained */
    size_t rules_explained;          /* Rules explained */
    size_t alternatives_considered;  /* Alternative paths considered */
    size_t errors_detected;          /* Errors detected in execution */
} explain_stats_t;

/* Opaque explanation engine structure */
typedef struct explain_engine explain_engine_t;

/* Creation and destruction */
explain_engine_t *explain_create(void);
void explain_destroy(explain_engine_t *engine);

/* Configuration */
void explain_set_mode(explain_engine_t *engine, explain_mode_t mode);
void explain_set_golf_mode(explain_engine_t *engine, bool golf_mode);
void explain_set_target(explain_engine_t *engine, const ast_node_t *target_node);
void explain_focus_node(explain_engine_t *engine, const ast_node_t *node);

/* Explanation generation */
void explain_generate(explain_engine_t *engine, const ast_node_t *root, const trace_t *trace);

/* Result access */
const char *explain_get_output(const explain_engine_t *engine);
const explain_stats_t *explain_get_stats(const explain_engine_t *engine);
const char *explain_get_focused_output(explain_engine_t *engine, const ast_node_t *node);

/* Golf mode helpers */
void explain_add_golf_shortcut(explain_engine_t *engine, const char *shortcut, const char *meaning);

/* State management */
void explain_reset(explain_engine_t *engine);

/* Export and output */
bool explain_export_text(explain_engine_t *engine, const char *filename);
void explain_print(const explain_engine_t *engine, FILE *fp);

#endif /* REASONS_EXPLAIN_H */
