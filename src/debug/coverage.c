/*
 * coverage.c - Branch Coverage Analysis for Reasons Debugger
 * 
 * Features:
 * - Tracks visited nodes in decision trees
 * - Calculates node and branch coverage
 * - Identifies uncovered paths
 * - Supports multiple coverage strategies
 * - Generates detailed coverage reports
 * - Exports coverage data in various formats
 * - Integrates with debugger and test runner
 */

#include "reasons/debugger.h"
#include "reasons/tree.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    char *node_id;              // Node identifier
    unsigned visit_count;       // Number of times visited
    bool covered;               // Coverage status
} NodeCoverage;

typedef struct {
    char *from_node;            // Source node ID
    char *to_node;              // Target node ID
    bool covered;               // Coverage status
    unsigned traversal_count;   // Times traversed
} BranchCoverage;

struct CoverageData {
    hash_table_t *node_coverage;    // Node coverage data (key: node_id)
    vector_t *branch_coverage;      // Branch coverage data
    unsigned nodes_total;           // Total nodes in tree
    unsigned nodes_visited;         // Visited nodes
    unsigned branches_total;        // Total branches
    unsigned branches_visited;      // Visited branches
    unsigned conditions_total;      // Total condition nodes
    unsigned leaves_total;          // Total leaf nodes
    double start_time;              // Coverage session start time
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static NodeCoverage* create_node_coverage(const char *node_id) {
    NodeCoverage *nc = mem_alloc(sizeof(NodeCoverage));
    if (nc) {
        nc->node_id = string_duplicate(node_id);
        nc->visit_count = 0;
        nc->covered = false;
    }
    return nc;
}

static void destroy_node_coverage(void *data) {
    NodeCoverage *nc = (NodeCoverage*)data;
    if (nc) {
        if (nc->node_id) mem_free(nc->node_id);
        mem_free(nc);
    }
}

static BranchCoverage* create_branch_coverage(const char *from_node, const char *to_node) {
    BranchCoverage *bc = mem_alloc(sizeof(BranchCoverage));
    if (bc) {
        bc->from_node = string_duplicate(from_node);
        bc->to_node = string_duplicate(to_node);
        bc->covered = false;
        bc->traversal_count = 0;
    }
    return bc;
}

static void destroy_branch_coverage(void *data) {
    BranchCoverage *bc = (BranchCoverage*)data;
    if (bc) {
        if (bc->from_node) mem_free(bc->from_node);
        if (bc->to_node) mem_free(bc->to_node);
        mem_free(bc);
    }
}

static void analyze_tree_structure(CoverageData *cov, TreeNode *node) {
    if (!cov || !node) return;
    
    // Count node type
    switch (node->type) {
        case NODE_CONDITION:
            cov->conditions_total++;
            break;
        case NODE_LEAF:
            cov->leaves_total++;
            break;
        default:
            break;
    }
    
    // Recursively process children
    for (unsigned i = 0; i < vector_size(node->children); i++) {
        TreeNode *child = vector_at(node->children, i);
        analyze_tree_structure(cov, child);
    }
}

static void register_branches(CoverageData *cov, TreeNode *node) {
    if (!cov || !node || node->type != NODE_CONDITION) return;
    
    // Condition nodes always have at least true and false branches
    if (vector_size(node->children) >= 2) {
        TreeNode *true_child = vector_at(node->children, 0);
        TreeNode *false_child = vector_at(node->children, 1);
        
        // Create branch coverage entries
        BranchCoverage *bc_true = create_branch_coverage(node->id, true_child->id);
        BranchCoverage *bc_false = create_branch_coverage(node->id, false_child->id);
        
        if (bc_true) vector_append(cov->branch_coverage, bc_true);
        if (bc_false) vector_append(cov->branch_coverage, bc_false);
        
        cov->branches_total += 2;
    }
    
    // Process children recursively
    for (unsigned i = 0; i < vector_size(node->children); i++) {
        TreeNode *child = vector_at(node->children, i);
        register_branches(cov, child);
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

CoverageData* coverage_create(DecisionTree *tree) {
    if (!tree || !tree->root) {
        LOG_ERROR("Cannot create coverage for invalid tree");
        return NULL;
    }
    
    CoverageData *cov = mem_alloc(sizeof(CoverageData));
    if (!cov) {
        LOG_ERROR("Memory allocation failed for coverage data");
        return NULL;
    }
    
    // Initialize counts
    memset(cov, 0, sizeof(CoverageData));
    cov->start_time = get_current_timestamp();
    
    // Create data structures
    cov->node_coverage = hash_create(128, destroy_node_coverage);
    cov->branch_coverage = vector_create(64);
    
    if (!cov->node_coverage || !cov->branch_coverage) {
        LOG_ERROR("Failed to create coverage data structures");
        coverage_destroy(cov);
        return NULL;
    }
    
    // Analyze tree structure
    cov->nodes_total = tree_total_nodes(tree);
    analyze_tree_structure(cov, tree->root);
    register_branches(cov, tree->root);
    
    return cov;
}

void coverage_destroy(CoverageData *cov) {
    if (!cov) return;
    
    if (cov->node_coverage) hash_destroy(cov->node_coverage);
    if (cov->branch_coverage) vector_destroy_custom(cov->branch_coverage, destroy_branch_coverage);
    mem_free(cov);
}

void coverage_reset(CoverageData *cov) {
    if (!cov) return;
    
    // Reset node coverage
    const char *key;
    NodeCoverage *nc;
    hash_iter_t iter = hash_iter(cov->node_coverage);
    while (hash_next(cov->node_coverage, &iter, &key, (void**)&nc)) {
        nc->visit_count = 0;
        nc->covered = false;
    }
    
    // Reset branch coverage
    for (size_t i = 0; i < vector_size(cov->branch_coverage); i++) {
        BranchCoverage *bc = vector_at(cov->branch_coverage, i);
        bc->covered = false;
        bc->traversal_count = 0;
    }
    
    // Reset counters
    cov->nodes_visited = 0;
    cov->branches_visited = 0;
    cov->start_time = get_current_timestamp();
}

void coverage_record_node(CoverageData *cov, TreeNode *node) {
    if (!cov || !node || !node->id) return;
    
    // Get or create node coverage entry
    NodeCoverage *nc = hash_get(cov->node_coverage, node->id);
    if (!nc) {
        nc = create_node_coverage(node->id);
        if (!nc) return;
        hash_set(cov->node_coverage, nc->node_id, nc);
    }
    
    // Update coverage data
    nc->visit_count++;
    if (!nc->covered) {
        nc->covered = true;
        cov->nodes_visited++;
    }
}

void coverage_record_branch(CoverageData *cov, TreeNode *from_node, TreeNode *to_node) {
    if (!cov || !from_node || !to_node || !from_node->id || !to_node->id) return;
    
    // Find matching branch coverage entry
    for (size_t i = 0; i < vector_size(cov->branch_coverage); i++) {
        BranchCoverage *bc = vector_at(cov->branch_coverage, i);
        
        if (strcmp(bc->from_node, from_node->id) == 0 &&
            strcmp(bc->to_node, to_node->id) == 0) {
            
            bc->traversal_count++;
            if (!bc->covered) {
                bc->covered = true;
                cov->branches_visited++;
            }
            return;
        }
    }
    
    // If we get here, the branch wasn't pre-registered - create new entry
    BranchCoverage *bc = create_branch_coverage(from_node->id, to_node->id);
    if (bc) {
        bc->covered = true;
        bc->traversal_count = 1;
        vector_append(cov->branch_coverage, bc);
        cov->branches_visited++;
        cov->branches_total++;
    }
}

void coverage_calculate(CoverageData *cov) {
    // Most calculations are done in real-time
    // This function can be used for more complex metrics if needed
}

double coverage_node_percentage(CoverageData *cov) {
    if (!cov || cov->nodes_total == 0) return 0.0;
    return (double)cov->nodes_visited / cov->nodes_total * 100.0;
}

double coverage_branch_percentage(CoverageData *cov) {
    if (!cov || cov->branches_total == 0) return 0.0;
    return (double)cov->branches_visited / cov->branches_total * 100.0;
}

void coverage_print_report(CoverageData *cov, FILE *output) {
    if (!cov || !output) return;
    
    double node_pct = coverage_node_percentage(cov);
    double branch_pct = coverage_branch_percentage(cov);
    double duration = get_current_timestamp() - cov->start_time;
    
    fprintf(output, "\nCoverage Report\n");
    fprintf(output, "===============\n");
    fprintf(output, "  Duration:         %.3f seconds\n", duration);
    fprintf(output, "  Nodes total:      %u\n", cov->nodes_total);
    fprintf(output, "  Nodes visited:    %u (%.2f%%)\n", cov->nodes_visited, node_pct);
    fprintf(output, "  Branches total:   %u\n", cov->branches_total);
    fprintf(output, "  Branches visited: %u (%.2f%%)\n", cov->branches_visited, branch_pct);
    fprintf(output, "  Condition nodes:  %u\n", cov->conditions_total);
    fprintf(output, "  Leaf nodes:       %u\n", cov->leaves_total);
    
    // Print uncovered nodes if any
    if (cov->nodes_visited < cov->nodes_total) {
        fprintf(output, "\nUncovered Nodes:\n");
        const char *key;
        NodeCoverage *nc;
        hash_iter_t iter = hash_iter(cov->node_coverage);
        while (hash_next(cov->node_coverage, &iter, &key, (void**)&nc)) {
            if (!nc->covered) {
                fprintf(output, "  - %s\n", nc->node_id);
            }
        }
    }
    
    // Print uncovered branches if any
    if (cov->branches_visited < cov->branches_total) {
        fprintf(output, "\nUncovered Branches:\n");
        for (size_t i = 0; i < vector_size(cov->branch_coverage); i++) {
            BranchCoverage *bc = vector_at(cov->branch_coverage, i);
            if (!bc->covered) {
                fprintf(output, "  - %s -> %s\n", bc->from_node, bc->to_node);
            }
        }
    }
    
    fprintf(output, "\n");
}

void coverage_export_json(CoverageData *cov, FILE *output) {
    if (!cov || !output) return;
    
    double node_pct = coverage_node_percentage(cov);
    double branch_pct = coverage_branch_percentage(cov);
    double duration = get_current_timestamp() - cov->start_time;
    
    fprintf(output, "{\n");
    fprintf(output, "  \"coverage_report\": {\n");
    fprintf(output, "    \"duration_seconds\": %.3f,\n", duration);
    fprintf(output, "    \"nodes_total\": %u,\n", cov->nodes_total);
    fprintf(output, "    \"nodes_visited\": %u,\n", cov->nodes_visited);
    fprintf(output, "    \"node_coverage_percentage\": %.2f,\n", node_pct);
    fprintf(output, "    \"branches_total\": %u,\n", cov->branches_total);
    fprintf(output, "    \"branches_visited\": %u,\n", cov->branches_visited);
    fprintf(output, "    \"branch_coverage_percentage\": %.2f,\n", branch_pct);
    fprintf(output, "    \"condition_nodes\": %u,\n", cov->conditions_total);
    fprintf(output, "    \"leaf_nodes\": %u,\n", cov->leaves_total);
    
    // Node coverage details
    fprintf(output, "    \"node_coverage\": [\n");
    const char *key;
    NodeCoverage *nc;
    hash_iter_t iter = hash_iter(cov->node_coverage);
    bool first_node = true;
    while (hash_next(cov->node_coverage, &iter, &key, (void**)&nc)) {
        if (!first_node) fprintf(output, ",\n");
        fprintf(output, "      {\n");
        fprintf(output, "        \"node_id\": \"%s\",\n", nc->node_id);
        fprintf(output, "        \"visit_count\": %u,\n", nc->visit_count);
        fprintf(output, "        \"covered\": %s\n", nc->covered ? "true" : "false");
        fprintf(output, "      }");
        first_node = false;
    }
    fprintf(output, "\n    ],\n");
    
    // Branch coverage details
    fprintf(output, "    \"branch_coverage\": [\n");
    for (size_t i = 0; i < vector_size(cov->branch_coverage); i++) {
        BranchCoverage *bc = vector_at(cov->branch_coverage, i);
        fprintf(output, "      {\n");
        fprintf(output, "        \"from_node\": \"%s\",\n", bc->from_node);
        fprintf(output, "        \"to_node\": \"%s\",\n", bc->to_node);
        fprintf(output, "        \"traversal_count\": %u,\n", bc->traversal_count);
        fprintf(output, "        \"covered\": %s\n", bc->covered ? "true" : "false");
        fprintf(output, "      }%s\n", i < vector_size(cov->branch_coverage)-1 ? "," : "");
    }
    fprintf(output, "    ]\n");
    
    fprintf(output, "  }\n");
    fprintf(output, "}\n");
}

const NodeCoverage* coverage_get_node_data(CoverageData *cov, const char *node_id) {
    if (!cov || !node_id) return NULL;
    return hash_get(cov->node_coverage, node_id);
}

unsigned coverage_get_visit_count(CoverageData *cov, const char *node_id) {
    const NodeCoverage *nc = coverage_get_node_data(cov, node_id);
    return nc ? nc->visit_count : 0;
}

bool coverage_is_node_covered(CoverageData *cov, const char *node_id) {
    const NodeCoverage *nc = coverage_get_node_data(cov, node_id);
    return nc ? nc->covered : false;
}

bool coverage_is_branch_covered(CoverageData *cov, const char *from_node, const char *to_node) {
    if (!cov || !from_node || !to_node) return false;
    
    for (size_t i = 0; i < vector_size(cov->branch_coverage); i++) {
        BranchCoverage *bc = vector_at(cov->branch_coverage, i);
        if (strcmp(bc->from_node, from_node) == 0 &&
            strcmp(bc->to_node, to_node) == 0) {
            return bc->covered;
        }
    }
    return false;
}

void coverage_merge(CoverageData *dest, CoverageData *src) {
    if (!dest || !src) return;
    
    // Merge node coverage
    const char *key;
    NodeCoverage *src_nc;
    hash_iter_t iter = hash_iter(src->node_coverage);
    while (hash_next(src->node_coverage, &iter, &key, (void**)&src_nc)) {
        NodeCoverage *dest_nc = hash_get(dest->node_coverage, key);
        if (!dest_nc) {
            dest_nc = create_node_coverage(key);
            if (dest_nc) hash_set(dest->node_coverage, key, dest_nc);
        }
        
        if (dest_nc) {
            dest_nc->visit_count += src_nc->visit_count;
            if (src_nc->covered && !dest_nc->covered) {
                dest_nc->covered = true;
                dest->nodes_visited++;
            }
        }
    }
    
    // Merge branch coverage
    for (size_t i = 0; i < vector_size(src->branch_coverage); i++) {
        BranchCoverage *src_bc = vector_at(src->branch_coverage, i);
        bool found = false;
        
        // Find matching branch in destination
        for (size_t j = 0; j < vector_size(dest->branch_coverage); j++) {
            BranchCoverage *dest_bc = vector_at(dest->branch_coverage, j);
            if (strcmp(dest_bc->from_node, src_bc->from_node) == 0 &&
                strcmp(dest_bc->to_node, src_bc->to_node) == 0) {
                
                dest_bc->traversal_count += src_bc->traversal_count;
                if (src_bc->covered && !dest_bc->covered) {
                    dest_bc->covered = true;
                    dest->branches_visited++;
                }
                found = true;
                break;
            }
        }
        
        // Add new branch if not found
        if (!found) {
            BranchCoverage *new_bc = create_branch_coverage(src_bc->from_node, src_bc->to_node);
            if (new_bc) {
                new_bc->traversal_count = src_bc->traversal_count;
                new_bc->covered = src_bc->covered;
                vector_append(dest->branch_coverage, new_bc);
                
                if (src_bc->covered) {
                    dest->branches_visited++;
                }
                dest->branches_total++;
            }
        }
    }
}
