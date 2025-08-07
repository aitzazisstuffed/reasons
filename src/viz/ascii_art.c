/*
 * ascii_art.c - ASCII Art Tree Rendering for Reasons DSL
 *
 * Features:
 * - Compact tree visualization for terminals
 * - Customizable rendering options
 * - Depth limitation
 * - Node highlighting
 * - Multi-child node support
 * - Node value/description display
 * - Dynamic width calculation
 * - Horizontal and vertical tree layouts
 */

#include "reasons/viz.h"
#include "reasons/tree.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    char **grid;           // 2D grid for rendering
    int width;             // Current grid width
    int height;            // Current grid height
    int x_offset;          // Current x offset
    int y_offset;          // Current y offset
    int max_width;         // Maximum allowed width
    int max_depth;         // Maximum rendering depth
    bool compact;          // Compact rendering mode
} AsciiGrid;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static void grid_ensure_size(AsciiGrid *grid, int width, int height) {
    if (width <= grid->width && height <= grid->height) {
        return;
    }

    int new_width = (width > grid->width) ? width * 2 : grid->width;
    int new_height = (height > grid->height) ? height * 2 : grid->height;

    char **new_grid = mem_calloc(new_height, sizeof(char *));
    if (!new_grid) return;

    for (int y = 0; y < new_height; y++) {
        new_grid[y] = mem_calloc(new_width, sizeof(char));
        if (!new_grid[y]) {
            for (int i = 0; i < y; i++) mem_free(new_grid[i]);
            mem_free(new_grid);
            return;
        }
        
        // Initialize with spaces
        memset(new_grid[y], ' ', new_width);
        
        // Copy existing content
        if (y < grid->height) {
            int copy_width = (grid->width < new_width) ? grid->width : new_width;
            memcpy(new_grid[y], grid->grid[y], copy_width);
        }
    }

    // Free old grid
    if (grid->grid) {
        for (int y = 0; y < grid->height; y++) {
            mem_free(grid->grid[y]);
        }
        mem_free(grid->grid);
    }

    grid->grid = new_grid;
    grid->width = new_width;
    grid->height = new_height;
}

static void grid_put_char(AsciiGrid *grid, int x, int y, char c) {
    if (x < 0 || y < 0 || x >= grid->width || y >= grid->height) {
        return;
    }
    grid->grid[y][x] = c;
}

static void grid_put_string(AsciiGrid *grid, int x, int y, const char *str) {
    if (!str) return;
    for (int i = 0; str[i] != '\0'; i++) {
        grid_put_char(grid, x + i, y, str[i]);
    }
}

static void grid_draw_hline(AsciiGrid *grid, int x1, int x2, int y) {
    if (x1 > x2) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    
    for (int x = x1; x <= x2; x++) {
        if (grid->grid[y][x] == ' ') {
            grid_put_char(grid, x, y, '-');
        } else if (grid->grid[y][x] == '|') {
            grid_put_char(grid, x, y, '+');
        }
    }
}

static void grid_draw_vline(AsciiGrid *grid, int x, int y1, int y2) {
    if (y1 > y2) {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    
    for (int y = y1; y <= y2; y++) {
        if (grid->grid[y][x] == ' ') {
            grid_put_char(grid, x, y, '|');
        } else if (grid->grid[y][x] == '-') {
            grid_put_char(grid, x, y, '+');
        }
    }
}

static int node_label_width(TreeNode *node, AsciiRenderContext *ctx) {
    int width = 0;
    
    if (node->id) {
        width += strlen(node->id);
    }
    
    if (ctx->show_descriptions && node->description) {
        width += strlen(node->description) + 2; // +2 for parentheses
    }
    
    if (ctx->show_values && node->value.type != VALUE_NULL) {
        char value_buf[128];
        reasons_value_format(&node->value, value_buf, sizeof(value_buf));
        width += strlen(value_buf) + 3; // +3 for " = "
    }
    
    // Highlight marker
    if (ctx->highlight_path && ctx->highlight_nodes) {
        for (size_t i = 0; i < vector_size(ctx->highlight_nodes); i++) {
            if (vector_at(ctx->highlight_nodes, i) == node) {
                width += 2; // For "* " prefix
                break;
            }
        }
    }
    
    return width;
}

static void render_node(AsciiGrid *grid, TreeNode *node, int x, int y, AsciiRenderContext *ctx) {
    char label_buf[256] = {0};
    char *ptr = label_buf;
    
    // Highlight marker
    bool is_highlighted = false;
    if (ctx->highlight_path && ctx->highlight_nodes) {
        for (size_t i = 0; i < vector_size(ctx->highlight_nodes); i++) {
            if (vector_at(ctx->highlight_nodes, i) == node) {
                *ptr++ = '*';
                *ptr++ = ' ';
                is_highlighted = true;
                break;
            }
        }
    }
    
    // Node ID
    if (node->id) {
        strcpy(ptr, node->id);
        ptr += strlen(node->id);
    } else {
        strcpy(ptr, "?");
        ptr++;
    }
    
    // Description
    if (ctx->show_descriptions && node->description) {
        *ptr++ = ' ';
        *ptr++ = '(';
        strcpy(ptr, node->description);
        ptr += strlen(node->description);
        *ptr++ = ')';
    }
    
    // Value
    if (ctx->show_values && node->value.type != VALUE_NULL) {
        strcpy(ptr, " = ");
        ptr += 3;
        char value_buf[128];
        reasons_value_format(&node->value, value_buf, sizeof(value_buf));
        strcpy(ptr, value_buf);
        ptr += strlen(value_buf);
    }
    
    // Draw the label
    grid_put_string(grid, x - strlen(label_buf)/2, y, label_buf);
    
    // Draw box around node if highlighted
    if (is_highlighted) {
        int len = strlen(label_buf);
        int left = x - len/2 - 1;
        int right = x + (len+1)/2;
        
        grid_put_char(grid, left, y-1, '+');
        grid_put_char(grid, right, y-1, '+');
        grid_put_char(grid, left, y+1, '+');
        grid_put_char(grid, right, y+1, '+');
        
        grid_draw_hline(grid, left+1, right-1, y-1);
        grid_draw_hline(grid, left+1, right-1, y+1);
        grid_draw_vline(grid, left, y-1, y+1);
        grid_draw_vline(grid, right, y-1, y+1);
    }
}

static int layout_tree(AsciiGrid *grid, TreeNode *node, int x, int y, 
                      AsciiRenderContext *ctx, int depth, bool *node_counts) {
    if (ctx->max_depth > 0 && depth > ctx->max_depth) {
        return x;
    }
    
    // Ensure grid size
    grid_ensure_size(grid, x + 20, y + 5);
    
    // Render this node
    render_node(grid, node, x, y, ctx);
    
    int num_children = vector_size(node->children);
    if (num_children == 0 || (ctx->max_depth > 0 && depth == ctx->max_depth)) {
        if (node_counts) node_counts[depth]++;
        return x;
    }
    
    int child_y = y + 2;
    int child_spacing = ctx->compact ? 4 : 8;
    int total_child_width = 0;
    int *child_positions = mem_alloc(num_children * sizeof(int));
    
    // Calculate positions for children
    for (int i = 0; i < num_children; i++) {
        TreeNode *child = vector_at(node->children, i);
        int child_width = node_label_width(child, ctx);
        total_child_width += child_width + child_spacing;
    }
    total_child_width -= child_spacing; // Remove last spacing
    
    int start_x = x - total_child_width / 2;
    int current_x = start_x;
    
    // Render children and connect lines
    for (int i = 0; i < num_children; i++) {
        TreeNode *child = vector_at(node->children, i);
        int child_width = node_label_width(child, ctx);
        int child_x = current_x + child_width / 2;
        
        // Draw connection line
        if (num_children == 1) {
            grid_draw_vline(grid, x, y+1, child_y-1);
        } else {
            grid_draw_vline(grid, x, y+1, y+1 + (child_y - y - 2)/2);
            grid_draw_hline(grid, x, child_x, y+1 + (child_y - y - 2)/2);
            grid_draw_vline(grid, child_x, y+1 + (child_y - y - 2)/2, child_y-1);
        }
        
        // Render child
        child_positions[i] = layout_tree(grid, child, child_x, child_y, 
                                        ctx, depth+1, node_counts);
        
        // Draw edge label
        if (node->type == NODE_CONDITION && node->edge_labels) {
            const char *label = vector_at(node->edge_labels, i);
            if (label) {
                int label_x = (x + child_x) / 2;
                int label_y = y + 1 + (child_y - y - 2)/2;
                grid_put_string(grid, label_x - strlen(label)/2, label_y, label);
            }
        }
        
        current_x += child_width + child_spacing;
    }
    
    if (node_counts) node_counts[depth]++;
    mem_free(child_positions);
    return x;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

void render_ascii_tree(TreeNode *root, AsciiRenderContext *ctx) {
    if (!root || !ctx || !ctx->output) return;
    
    AsciiGrid grid = {0};
    grid.max_width = ctx->compact ? 120 : 200;
    grid.max_depth = ctx->max_depth;
    
    // Initialize grid
    grid_ensure_size(&grid, 80, 20);
    
    // Layout the tree
    layout_tree(&grid, root, grid.width/2, 1, ctx, 0, NULL);
    
    // Find actual content bounds
    int min_x = grid.width, max_x = 0;
    int min_y = grid.height, max_y = 0;
    bool found_content = false;
    
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            if (grid.grid[y][x] != ' ') {
                found_content = true;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    
    // If no content, return
    if (!found_content) {
        fputs("(empty tree)\n", ctx->output);
        goto cleanup;
    }
    
    // Print the grid
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            fputc(grid.grid[y][x], ctx->output);
        }
        fputc('\n', ctx->output);
    }
    
cleanup:
    // Clean up grid memory
    if (grid.grid) {
        for (int y = 0; y < grid.height; y++) {
            mem_free(grid.grid[y]);
        }
        mem_free(grid.grid);
    }
}

char* ascii_tree_to_string(TreeNode *root, AsciiRenderContext *ctx) {
    if (!root || !ctx) return NULL;
    
    // Redirect output to memory buffer
    FILE *original_output = ctx->output;
    FILE *mem_stream = open_memstream(NULL, NULL);
    if (!mem_stream) return NULL;
    
    ctx->output = mem_stream;
    render_ascii_tree(root, ctx);
    fclose(mem_stream);
    
    // Restore original output
    ctx->output = original_output;
    
    char *result = NULL;
    size_t size;
    FILE *tmp = open_memstream(&result, &size);
    if (tmp) {
        fclose(tmp);
    }
    
    return result;
}
