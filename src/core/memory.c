/*
 * memory.c - Advanced Memory Management for Reasons DSL
 * 
 * Features:
 * - Allocation tracking with source location
 * - Memory leak detection
 * - Guard pages for buffer overflow detection
 * - Custom allocators (arena, pool)
 * - Statistics and reporting
 * - Garbage collection integration
 */

#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/error.h"
#include "utils/collections.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

/* Allocation metadata header */
typedef struct MemHeader {
    size_t size;                // Requested size
    const char *file;           // Source file
    int line;                   // Source line
    unsigned magic;             // Magic number for validation
    struct MemHeader *next;     // Next in allocation list
    struct MemHeader *prev;     // Previous in allocation list
} MemHeader;

/* Allocation footer for overflow detection */
typedef struct {
    unsigned guard;             // Guard pattern
} MemFooter;

/* Memory statistics */
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

/* Memory arena structure */
typedef struct {
    char *memory;               // Arena memory block
    size_t size;                // Total arena size
    size_t offset;              // Current allocation offset
    const char *name;           // Arena name
} MemArena;

/* Memory pool structure */
typedef struct {
    size_t block_size;          // Fixed block size
    size_t block_count;         // Number of blocks
    void **free_list;           // Free list
    char *memory;               // Pool memory block
    const char *name;           // Pool name
} MemPool;

/* Global memory state */
static struct {
    MemHeader *allocations;     // List of active allocations
    MemStats stats;             // Memory statistics
    bool tracking_enabled;      // Allocation tracking status
    bool guard_pages_enabled;   // Buffer overflow protection
    size_t guard_page_size;     // System page size
    vector_t *arenas;           // Active memory arenas
    vector_t *pools;            // Active memory pools
} g_memory = {0};

/* Constants */
#define MEM_MAGIC 0xABCD1234
#define GUARD_MAGIC 0xDEADBEEF
#define DEFAULT_ARENA_SIZE (2 * 1024 * 1024) // 2MB
#define POOL_GROW_SIZE 32

/* Forward declarations */
static void memory_track_allocation(MemHeader *header, size_t size, 
                                   const char *file, int line);
static void memory_untrack_allocation(MemHeader *header);
static void memory_check_guard(MemFooter *footer);
static MemArena* arena_create_internal(size_t size, const char *name);
static void* arena_alloc_internal(MemArena *arena, size_t size, 
                                 size_t alignment, const char *file, int line);
static MemPool* pool_create_internal(size_t block_size, size_t initial_blocks, 
                                    const char *name);
static void* pool_alloc_internal(MemPool *pool, const char *file, int line);
static void pool_free_internal(MemPool *pool, void *block);

/* Initialization */
void memory_init(void) {
    g_memory.tracking_enabled = true;
    g_memory.guard_pages_enabled = true;
    g_memory.guard_page_size = sysconf(_SC_PAGESIZE);
    g_memory.arenas = vector_create(4);
    g_memory.pools = vector_create(4);
    
    // Create default arenas
    vector_append(g_memory.arenas, arena_create_internal(DEFAULT_ARENA_SIZE, "default"));
    vector_append(g_memory.arenas, arena_create_internal(DEFAULT_ARENA_SIZE, "temp"));
    
    LOG_INFO("Memory system initialized, page size: %zu", g_memory.guard_page_size);
}

void memory_shutdown(void) {
    // Report leaks before cleanup
    memory_report_leaks(stderr);
    
    // Destroy all arenas
    for (size_t i = 0; i < vector_size(g_memory.arenas); i++) {
        MemArena *arena = vector_at(g_memory.arenas, i);
        munmap(arena->memory, arena->size);
        mem_free(arena);
    }
    vector_destroy(g_memory.arenas);
    
    // Destroy all pools
    for (size_t i = 0; i < vector_size(g_memory.pools); i++) {
        MemPool *pool = vector_at(g_memory.pools, i);
        munmap(pool->memory, pool->block_size * pool->block_count);
        vector_destroy(pool->free_list);
        mem_free(pool);
    }
    vector_destroy(g_memory.pools);
    
    LOG_INFO("Memory system shutdown");
}

/* Core allocation functions */
void* memory_allocate(size_t size, const char *file, int line) {
    if (size == 0) return NULL;
    
    // Calculate total size with header and footer
    size_t total_size = sizeof(MemHeader) + size + (g_memory.guard_pages_enabled ? sizeof(MemFooter) : 0);
    
    // Allocate memory with guard pages if enabled
    void *block = NULL;
    if (g_memory.guard_pages_enabled) {
        // Allocate with guard pages
        size_t page_size = g_memory.guard_page_size;
        size_t allocate_size = ((total_size + page_size - 1) / page_size) * page_size + 2 * page_size;
        
        char *mem = mmap(NULL, allocate_size, PROT_READ | PROT_WRITE, 
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            LOG_ERROR("mmap failed: %s", strerror(errno));
            return NULL;
        }
        
        // Protect guard pages
        mprotect(mem, page_size, PROT_NONE);
        mprotect(mem + allocate_size - page_size, page_size, PROT_NONE);
        
        block = mem + page_size;
    } else {
        block = malloc(total_size);
    }
    
    if (!block) {
        LOG_ERROR("Allocation failed: %zu bytes", size);
        return NULL;
    }
    
    // Set up header
    MemHeader *header = (MemHeader*)block;
    header->size = size;
    header->file = file;
    header->line = line;
    header->magic = MEM_MAGIC;
    
    // Set up footer if enabled
    if (g_memory.guard_pages_enabled) {
        MemFooter *footer = (MemFooter*)((char*)block + sizeof(MemHeader) + size);
        footer->guard = GUARD_MAGIC;
    }
    
    // Track allocation
    if (g_memory.tracking_enabled) {
        memory_track_allocation(header, size, file, line);
    }
    
    // Return pointer after header
    return (void*)((char*)block + sizeof(MemHeader));
}

void* memory_reallocate(void *ptr, size_t new_size, const char *file, int line) {
    if (!ptr) return memory_allocate(new_size, file, line);
    if (new_size == 0) {
        memory_free(ptr, file, line);
        return NULL;
    }
    
    // Get header
    MemHeader *header = (MemHeader*)((char*)ptr - sizeof(MemHeader));
    
    // Validate magic number
    if (header->magic != MEM_MAGIC) {
        LOG_ERROR("Invalid realloc: corrupted header or bad pointer");
        return NULL;
    }
    
    // Check guard if enabled
    if (g_memory.guard_pages_enabled) {
        MemFooter *footer = (MemFooter*)((char*)ptr + header->size);
        memory_check_guard(footer);
    }
    
    // Allocate new block
    void *new_ptr = memory_allocate(new_size, file, line);
    if (!new_ptr) return NULL;
    
    // Copy data
    size_t copy_size = header->size < new_size ? header->size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    
    // Free old block
    memory_free(ptr, file, line);
    
    // Update statistics
    g_memory.stats.realloc_count++;
    
    return new_ptr;
}

void memory_free(void *ptr, const char *file, int line) {
    if (!ptr) return;
    
    // Get header
    MemHeader *header = (MemHeader*)((char*)ptr - sizeof(MemHeader));
    
    // Validate magic number
    if (header->magic != MEM_MAGIC) {
        LOG_ERROR("Invalid free: corrupted header or bad pointer at %s:%d", file, line);
        return;
    }
    
    // Check guard if enabled
    if (g_memory.guard_pages_enabled) {
        MemFooter *footer = (MemFooter*)((char*)ptr + header->size);
        memory_check_guard(footer);
    }
    
    // Untrack allocation
    if (g_memory.tracking_enabled) {
        memory_untrack_allocation(header);
    }
    
    // Actually free memory
    if (g_memory.guard_pages_enabled) {
        size_t page_size = g_memory.guard_page_size;
        char *mem = (char*)header - page_size;
        size_t total_size = sizeof(MemHeader) + header->size + sizeof(MemFooter) + 2 * page_size;
        munmap(mem, total_size);
    } else {
        free(header);
    }
}

/* Memory arenas */
MemArena* arena_create(size_t size, const char *name) {
    MemArena *arena = arena_create_internal(size, name);
    if (arena) {
        vector_append(g_memory.arenas, arena);
    }
    return arena;
}

void* arena_alloc(MemArena *arena, size_t size, const char *file, int line) {
    return arena_alloc_internal(arena, size, 1, file, line);
}

void* arena_alloc_aligned(MemArena *arena, size_t size, size_t alignment, 
                         const char *file, int line) {
    return arena_alloc_internal(arena, size, alignment, file, line);
}

void arena_reset(MemArena *arena) {
    if (arena) {
        arena->offset = 0;
        LOG_DEBUG("Reset arena: %s", arena->name);
    }
}

/* Memory pools */
MemPool* pool_create(size_t block_size, size_t initial_blocks, const char *name) {
    MemPool *pool = pool_create_internal(block_size, initial_blocks, name);
    if (pool) {
        vector_append(g_memory.pools, pool);
    }
    return pool;
}

void* pool_alloc(MemPool *pool, const char *file, int line) {
    return pool_alloc_internal(pool, file, line);
}

void pool_free(MemPool *pool, void *block) {
    pool_free_internal(pool, block);
}

/* Statistics and reporting */
MemStats memory_get_stats(void) {
    return g_memory.stats;
}

void memory_report_leaks(FILE *output) {
    if (!g_memory.tracking_enabled) {
        fprintf(output, "Memory tracking is disabled\n");
        return;
    }
    
    MemHeader *current = g_memory.allocations;
    size_t leak_count = 0;
    size_t leak_bytes = 0;
    
    while (current) {
        fprintf(output, "LEAK: %zu bytes at %p allocated in %s:%d\n",
                current->size, (void*)((char*)current + sizeof(MemHeader)),
                current->file, current->line);
        
        leak_count++;
        leak_bytes += current->size;
        current = current->next;
    }
    
    if (leak_count > 0) {
        fprintf(output, "Total leaks: %zu (%zu bytes)\n", leak_count, leak_bytes);
        g_memory.stats.leak_count = leak_count;
    } else {
        fprintf(output, "No memory leaks detected\n");
    }
}

void memory_report(FILE *output) {
    MemStats stats = memory_get_stats();
    
    fprintf(output, "\n===== Memory Usage Report =====\n");
    fprintf(output, "Current allocated: %zu bytes\n", stats.current_allocated);
    fprintf(output, "Peak allocated:    %zu bytes\n", stats.peak_allocated);
    fprintf(output, "Total allocations: %zu\n", stats.allocation_count);
    fprintf(output, "Current blocks:    %zu\n", stats.allocation_count - stats.total_freed);
    fprintf(output, "Peak blocks:       %zu\n", stats.peak_allocation);
    fprintf(output, "Reallocations:     %zu\n", stats.realloc_count);
    fprintf(output, "Detected leaks:    %zu\n", stats.leak_count);
    
    // Arena usage
    for (size_t i = 0; i < vector_size(g_memory.arenas); i++) {
        MemArena *arena = vector_at(g_memory.arenas, i);
        double usage = (double)arena->offset / arena->size * 100.0;
        fprintf(output, "Arena '%s': %zu/%zu bytes (%.2f%%)\n", 
                arena->name, arena->offset, arena->size, usage);
    }
    
    // Pool usage
    for (size_t i = 0; i < vector_size(g_memory.pools); i++) {
        MemPool *pool = vector_at(g_memory.pools, i);
        size_t free_blocks = vector_size(pool->free_list);
        size_t used_blocks = pool->block_count - free_blocks;
        double usage = (double)used_blocks / pool->block_count * 100.0;
        fprintf(output, "Pool '%s': %zu/%zu blocks (%.2f%%)\n", 
                pool->name, used_blocks, pool->block_count, usage);
    }
    
    fprintf(output, "================================\n");
}

/* Configuration */
void memory_set_tracking(bool enabled) {
    g_memory.tracking_enabled = enabled;
}

void memory_set_guard_pages(bool enabled) {
    g_memory.guard_pages_enabled = enabled;
}

/* Internal functions */
static void memory_track_allocation(MemHeader *header, size_t size, 
                                   const char *file, int line) {
    // Add to allocation list
    header->next = g_memory.allocations;
    header->prev = NULL;
    
    if (g_memory.allocations) {
        g_memory.allocations->prev = header;
    }
    g_memory.allocations = header;
    
    // Update statistics
    g_memory.stats.total_allocated += size;
    g_memory.stats.current_allocated += size;
    g_memory.stats.allocation_count++;
    
    if (g_memory.stats.current_allocated > g_memory.stats.peak_allocated) {
        g_memory.stats.peak_allocated = g_memory.stats.current_allocated;
    }
    
    if (g_memory.stats.allocation_count > g_memory.stats.peak_allocation) {
        g_memory.stats.peak_allocation = g_memory.stats.allocation_count;
    }
}

static void memory_untrack_allocation(MemHeader *header) {
    // Remove from allocation list
    if (header->prev) {
        header->prev->next = header->next;
    } else {
        g_memory.allocations = header->next;
    }
    
    if (header->next) {
        header->next->prev = header->prev;
    }
    
    // Update statistics
    g_memory.stats.total_freed += header->size;
    g_memory.stats.current_allocated -= header->size;
    g_memory.stats.allocation_count--;
}

static void memory_check_guard(MemFooter *footer) {
    if (footer->guard != GUARD_MAGIC) {
        LOG_ERROR("Memory corruption detected: buffer overflow");
        // Trigger breakpoint or abort in debug mode
        #ifdef DEBUG
        __builtin_trap();
        #else
        error_set(ERROR_MEMORY_CORRUPTION, "Buffer overflow detected");
        #endif
    }
}

static MemArena* arena_create_internal(size_t size, const char *name) {
    if (size == 0) size = DEFAULT_ARENA_SIZE;
    
    char *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, 
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("Arena allocation failed: %s", strerror(errno));
        return NULL;
    }
    
    MemArena *arena = mem_alloc(sizeof(MemArena));
    if (!arena) {
        munmap(memory, size);
        return NULL;
    }
    
    arena->memory = memory;
    arena->size = size;
    arena->offset = 0;
    arena->name = name ? string_duplicate(name) : "unnamed";
    
    LOG_DEBUG("Created arena '%s' with %zu bytes", arena->name, arena->size);
    return arena;
}

static void* arena_alloc_internal(MemArena *arena, size_t size, 
                                 size_t alignment, const char *file, int line) {
    if (!arena || size == 0) return NULL;
    
    // Calculate aligned offset
    size_t aligned_offset = arena->offset;
    if (alignment > 1) {
        size_t mask = alignment - 1;
        aligned_offset = (arena->offset + mask) & ~mask;
    }
    
    // Check if we have enough space
    if (aligned_offset + size > arena->size) {
        LOG_ERROR("Arena '%s' out of memory: %zu bytes requested, %zu available",
                 arena->name, size, arena->size - arena->offset);
        return NULL;
    }
    
    // Get pointer and update offset
    void *ptr = arena->memory + aligned_offset;
    arena->offset = aligned_offset + size;
    
    // Update statistics
    g_memory.stats.total_allocated += size;
    g_memory.stats.current_allocated += size;
    g_memory.stats.allocation_count++;
    
    return ptr;
}

static MemPool* pool_create_internal(size_t block_size, size_t initial_blocks, 
                                    const char *name) {
    if (block_size == 0) return NULL;
    if (initial_blocks == 0) initial_blocks = POOL_GROW_SIZE;
    
    // Allocate memory for blocks
    size_t total_size = block_size * initial_blocks;
    char *memory = mmap(NULL, total_size, PROT_READ | PROT_WRITE, 
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("Pool allocation failed: %s", strerror(errno));
        return NULL;
    }
    
    // Create pool structure
    MemPool *pool = mem_alloc(sizeof(MemPool));
    if (!pool) {
        munmap(memory, total_size);
        return NULL;
    }
    
    // Initialize pool
    pool->block_size = block_size;
    pool->block_count = initial_blocks;
    pool->memory = memory;
    pool->name = name ? string_duplicate(name) : "unnamed";
    pool->free_list = vector_create(initial_blocks);
    
    // Add all blocks to free list
    for (size_t i = 0; i < initial_blocks; i++) {
        void *block = memory + i * block_size;
        vector_append(pool->free_list, block);
    }
    
    LOG_DEBUG("Created pool '%s' with %zu blocks of %zu bytes", 
             pool->name, pool->block_count, pool->block_size);
    return pool;
}

static void* pool_alloc_internal(MemPool *pool, const char *file, int line) {
    if (!pool) return NULL;
    
    // Grow pool if free list is empty
    if (vector_size(pool->free_list) == 0) {
        size_t new_blocks = POOL_GROW_SIZE;
        size_t new_size = pool->block_size * new_blocks;
        
        // Reallocate memory
        char *new_memory = mremap(pool->memory, 
                                 pool->block_size * pool->block_count,
                                 pool->block_size * pool->block_count + new_size,
                                 MREMAP_MAYMOVE);
        if (new_memory == MAP_FAILED) {
            LOG_ERROR("Pool expansion failed: %s", strerror(errno));
            return NULL;
        }
        
        pool->memory = new_memory;
        
        // Add new blocks to free list
        for (size_t i = 0; i < new_blocks; i++) {
            void *block = new_memory + (pool->block_count + i) * pool->block_size;
            vector_append(pool->free_list, block);
        }
        
        pool->block_count += new_blocks;
        LOG_DEBUG("Expanded pool '%s' to %zu blocks", pool->name, pool->block_count);
    }
    
    // Pop block from free list
    void *block = vector_pop(pool->free_list);
    
    // Update statistics
    g_memory.stats.total_allocated += pool->block_size;
    g_memory.stats.current_allocated += pool->block_size;
    g_memory.stats.allocation_count++;
    
    return block;
}

static void pool_free_internal(MemPool *pool, void *block) {
    if (!pool || !block) return;
    
    // Validate block address
    if ((char*)block < pool->memory || 
        (char*)block >= pool->memory + pool->block_size * pool->block_count) {
        LOG_ERROR("Invalid block freed to pool '%s'", pool->name);
        return;
    }
    
    // Add back to free list
    vector_append(pool->free_list, block);
    
    // Update statistics
    g_memory.stats.total_freed += pool->block_size;
    g_memory.stats.current_allocated -= pool->block_size;
    g_memory.stats.allocation_count--;
}
