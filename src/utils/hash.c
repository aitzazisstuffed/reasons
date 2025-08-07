/*
 * hash.c - Comprehensive Hash Table Implementation for Reasons DSL
 *
 * Features:
 * - Open addressing with linear probing
 * - Automatic resizing
 * - Custom hash functions
 * - Key deletion support
 * - Key iteration
 * - Memory efficiency
 * - Statistics collection
 * - Thread safety option
 */

#include "utils/hash.h"
#include "utils/memory.h"
#include "utils/error.h"
#include <stdlib.h>
#include <string.h>

/* ======== CONSTANTS ======== */

#define INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.75
#define GROWTH_FACTOR 2

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct HashEntry {
    void *key;
    void *value;
    size_t key_size;
    bool deleted;
} HashEntry;

struct HashTable {
    HashEntry *entries;
    size_t capacity;
    size_t size;
    size_t deleted_count;
    HashFunction hash_func;
    bool thread_safe;
    pthread_mutex_t lock;
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static uint32_t default_hash(const void *key, size_t key_size) {
    // FNV-1a hash
    const unsigned char *bytes = key;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < key_size; i++) {
        hash ^= bytes[i];
        hash *= 16777619;
    }
    return hash;
}

static bool keys_equal(const void *key1, size_t key1_size, 
                      const void *key2, size_t key2_size) {
    if (key1_size != key2_size) return false;
    return memcmp(key1, key2, key1_size) == 0;
}

static void hash_table_resize(HashTable *table, size_t new_capacity) {
    HashEntry *new_entries = mem_calloc(new_capacity, sizeof(HashEntry));
    if (!new_entries) return;
    
    // Rehash all entries
    for (size_t i = 0; i < table->capacity; i++) {
        HashEntry *entry = &table->entries[i];
        if (entry->key && !entry->deleted) {
            uint32_t hash = table->hash_func(entry->key, entry->key_size);
            size_t index = hash % new_capacity;
            
            // Find empty slot
            while (new_entries[index].key) {
                index = (index + 1) % new_capacity;
            }
            
            new_entries[index] = *entry;
        }
    }
    
    // Update table
    mem_free(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;
    table->deleted_count = 0;
}

static void hash_table_maybe_resize(HashTable *table) {
    size_t load = table->size + table->deleted_count;
    if (load > table->capacity * LOAD_FACTOR) {
        size_t new_capacity = table->capacity * GROWTH_FACTOR;
        hash_table_resize(table, new_capacity);
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

HashTable* hashtable_create(size_t initial_capacity, HashFunction hash_func) {
    HashTable *table = mem_alloc(sizeof(HashTable));
    if (!table) return NULL;
    
    size_t cap = initial_capacity > 0 ? initial_capacity : INITIAL_CAPACITY;
    table->entries = mem_calloc(cap, sizeof(HashEntry));
    if (!table->entries) {
        mem_free(table);
        return NULL;
    }
    
    table->capacity = cap;
    table->size = 0;
    table->deleted_count = 0;
    table->hash_func = hash_func ? hash_func : default_hash;
    table->thread_safe = false;
    
    return table;
}

void hashtable_destroy(HashTable *table) {
    if (!table) return;
    
    if (table->thread_safe) {
        pthread_mutex_destroy(&table->lock);
    }
    
    mem_free(table->entries);
    mem_free(table);
}

void hashtable_set_thread_safe(HashTable *table, bool thread_safe) {
    if (!table) return;
    
    if (thread_safe && !table->thread_safe) {
        pthread_mutex_init(&table->lock, NULL);
    } else if (!thread_safe && table->thread_safe) {
        pthread_mutex_destroy(&table->lock);
    }
    table->thread_safe = thread_safe;
}

void hashtable_set(HashTable *table, const void *key, size_t key_size, 
                  const void *value, size_t value_size) {
    if (!table || !key || key_size == 0) return;
    
    if (table->thread_safe) pthread_mutex_lock(&table->lock);
    
    // Resize if needed
    hash_table_maybe_resize(table);
    
    uint32_t hash = table->hash_func(key, key_size);
    size_t index = hash % table->capacity;
    size_t start_index = index;
    
    HashEntry *existing = NULL;
    HashEntry *first_deleted = NULL;
    
    // Find slot
    while (table->entries[index].key) {
        if (table->entries[index].deleted) {
            if (!first_deleted) first_deleted = &table->entries[index];
        } else if (keys_equal(key, key_size, table->entries[index].key, 
                             table->entries[index].key_size)) {
            existing = &table->entries[index];
            break;
        }
        
        index = (index + 1) % table->capacity;
        if (index == start_index) break; // Full table
    }
    
    HashEntry *entry;
    if (existing) {
        entry = existing;
        mem_free(entry->value);
    } else if (first_deleted) {
        entry = first_deleted;
        mem_free(entry->key);
        mem_free(entry->value);
        table->deleted_count--;
    } else {
        entry = &table->entries[index];
        table->size++;
    }
    
    // Set key and value
    entry->key = mem_alloc(key_size);
    if (!entry->key) {
        if (table->thread_safe) pthread_mutex_unlock(&table->lock);
        return;
    }
    memcpy(entry->key, key, key_size);
    entry->key_size = key_size;
    
    entry->value = mem_alloc(value_size);
    if (!entry->value) {
        mem_free(entry->key);
        if (table->thread_safe) pthread_mutex_unlock(&table->lock);
        return;
    }
    memcpy(entry->value, value, value_size);
    entry->deleted = false;
    
    if (table->thread_safe) pthread_mutex_unlock(&table->lock);
}

void* hashtable_get(HashTable *table, const void *key, size_t key_size) {
    if (!table || !key || key_size == 0) return NULL;
    
    if (table->thread_safe) pthread_mutex_lock(&table->lock);
    
    uint32_t hash = table->hash_func(key, key_size);
    size_t index = hash % table->capacity;
    size_t start_index = index;
    
    while (table->entries[index].key) {
        if (!table->entries[index].deleted && 
            keys_equal(key, key_size, table->entries[index].key, 
                      table->entries[index].key_size)) {
            if (table->thread_safe) pthread_mutex_unlock(&table->lock);
            return table->entries[index].value;
        }
        
        index = (index + 1) % table->capacity;
        if (index == start_index) break; // Full table
    }
    
    if (table->thread_safe) pthread_mutex_unlock(&table->lock);
    return NULL;
}

bool hashtable_remove(HashTable *table, const void *key, size_t key_size) {
    if (!table || !key || key_size == 0) return false;
    
    if (table->thread_safe) pthread_mutex_lock(&table->lock);
    
    uint32_t hash = table->hash_func(key, key_size);
    size_t index = hash % table->capacity;
    size_t start_index = index;
    
    while (table->entries[index].key) {
        if (!table->entries[index].deleted && 
            keys_equal(key, key_size, table->entries[index].key, 
                      table->entries[index].key_size)) {
            table->entries[index].deleted = true;
            table->size--;
            table->deleted_count++;
            
            // Cleanup if needed
            if (table->deleted_count > table->size) {
                hash_table_resize(table, table->capacity);
            }
            
            if (table->thread_safe) pthread_mutex_unlock(&table->lock);
            return true;
        }
        
        index = (index + 1) % table->capacity;
        if (index == start_index) break; // Full table
    }
    
    if (table->thread_safe) pthread_mutex_unlock(&table->lock);
    return false;
}

size_t hashtable_size(HashTable *table) {
    if (!table) return 0;
    return table->size;
}

void hashtable_clear(HashTable *table) {
    if (!table) return;
    
    if (table->thread_safe) pthread_mutex_lock(&table->lock);
    
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].key) {
            mem_free(table->entries[i].key);
            mem_free(table->entries[i].value);
            table->entries[i].key = NULL;
            table->entries[i].value = NULL;
            table->entries[i].deleted = false;
        }
    }
    table->size = 0;
    table->deleted_count = 0;
    
    if (table->thread_safe) pthread_mutex_unlock(&table->lock);
}

void hashtable_iterate(HashTable *table, HashIterCallback callback, void *user_data) {
    if (!table || !callback) return;
    
    if (table->thread_safe) pthread_mutex_lock(&table->lock);
    
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].key && !table->entries[i].deleted) {
            callback(table->entries[i].key, table->entries[i].key_size,
                     table->entries[i].value, user_data);
        }
    }
    
    if (table->thread_safe) pthread_mutex_unlock(&table->lock);
}
