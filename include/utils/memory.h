#ifndef UTILS_MEMORY_H
#define UTILS_MEMORY_H

#include <stddef.h>
#include <stdbool.h>

/* ======== PUBLIC INTERFACE ======== */

/**
 * Initializes the memory management system
 */
void memory_init(void);

/**
 * Shuts down the memory management system and cleans up resources
 */
void memory_shutdown(void);

/**
 * Allocates memory with tracking
 * 
 * @param size Number of bytes to allocate
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 * @return Pointer to allocated memory or NULL on failure
 */
void* memory_allocate(size_t size, const char *file, int line);

/**
 * Reallocates memory with tracking
 * 
 * @param ptr Pointer to previously allocated memory
 * @param new_size New size in bytes
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 * @return Pointer to reallocated memory or NULL on failure
 */
void* memory_reallocate(void *ptr, size_t new_size, const char *file, int line);

/**
 * Frees memory allocated with memory_allocate
 * 
 * @param ptr Pointer to memory to free
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 */
void memory_free(void *ptr, const char *file, int line);

/* ======== CONVENIENCE MACROS ======== */

#define mem_alloc(size) memory_allocate((size), __FILE__, __LINE__)
#define mem_realloc(ptr, size) memory_reallocate((ptr), (size), __FILE__, __LINE__)
#define mem_free(ptr) memory_free((ptr), __FILE__, __LINE__)

/* ======== ARENA ALLOCATOR INTERFACE ======== */

typedef struct MemArena MemArena;

/**
 * Creates a memory arena
 * 
 * @param size Arena size in bytes (0 for default)
 * @param name Arena name for debugging
 * @return New arena instance or NULL on failure
 */
MemArena* arena_create(size_t size, const char *name);

/**
 * Allocates memory from an arena
 * 
 * @param arena Arena instance
 * @param size Number of bytes to allocate
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 * @return Pointer to allocated memory or NULL on failure
 */
void* arena_alloc(MemArena *arena, size_t size, const char *file, int line);

/**
 * Allocates aligned memory from an arena
 * 
 * @param arena Arena instance
 * @param size Number of bytes to allocate
 * @param alignment Alignment requirement (power of 2)
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 * @return Pointer to allocated memory or NULL on failure
 */
void* arena_alloc_aligned(MemArena *arena, size_t size, size_t alignment, 
                         const char *file, int line);

/**
 * Resets an arena (frees all allocations within arena)
 * 
 * @param arena Arena instance to reset
 */
void arena_reset(MemArena *arena);

/* ======== POOL ALLOCATOR INTERFACE ======== */

typedef struct MemPool MemPool;

/**
 * Creates a memory pool
 * 
 * @param block_size Size of each block in bytes
 * @param initial_blocks Initial number of blocks (0 for default)
 * @param name Pool name for debugging
 * @return New pool instance or NULL on failure
 */
MemPool* pool_create(size_t block_size, size_t initial_blocks, const char *name);

/**
 * Allocates a block from a pool
 * 
 * @param pool Pool instance
 * @param file Source file name (automatically passed by macro)
 * @param line Source line number (automatically passed by macro)
 * @return Pointer to allocated block or NULL on failure
 */
void* pool_alloc(MemPool *pool, const char *file, int line);

/**
 * Returns a block to a pool
 * 
 * @param pool Pool instance
 * @param block Block to return to the pool
 */
void pool_free(MemPool *pool, void *block);

/* ======== STATISTICS AND REPORTING ======== */

typedef struct {
    size_t total_allocated;     // Total bytes allocated
    size_t total_freed;         // Total bytes freed
    size_t current_allocated;   // Currently allocated bytes
    size_t peak_allocated;      // Peak allocated bytes
    size_t allocation_count;    // Current allocations
    size_t peak_allocation;     // Peak allocation count
    size_t realloc_count;       // Reallocation operations
    size_t leak_count;          // Detected leaks
} MemStats;

/**
 * Retrieves current memory statistics
 * 
 * @return Memory statistics structure
 */
MemStats memory_get_stats(void);

/**
 * Reports memory leaks to an output stream
 * 
 * @param output Output stream (e.g., stderr)
 */
void memory_report_leaks(FILE *output);

/**
 * Reports comprehensive memory usage information
 * 
 * @param output Output stream (e.g., stdout)
 */
void memory_report(FILE *output);

/* ======== CONFIGURATION ======== */

/**
 * Enables or disables allocation tracking
 * 
 * @param enabled True to enable tracking, false to disable
 */
void memory_set_tracking(bool enabled);

/**
 * Enables or disables guard pages for overflow detection
 * 
 * @param enabled True to enable guard pages, false to disable
 */
void memory_set_guard_pages(bool enabled);

#endif /* UTILS_MEMORY_H */
