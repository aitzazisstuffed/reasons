/*
 * vector.c - Comprehensive Dynamic Array Implementation for Reasons DSL
 *
 * Features:
 * - Dynamic resizing
 * - Type-safe operations
 * - Custom element size
 * - Deep copy support
 * - Sorting and searching
 * - Stack operations
 * - Range operations
 * - Memory efficiency
 * - Iterator support
 */

#include "utils/vector.h"
#include "utils/memory.h"
#include "utils/error.h"
#include <stdlib.h>
#include <string.h>

/* ======== STRUCTURE DEFINITIONS ======== */

struct Vector {
    void *data;
    size_t size;
    size_t capacity;
    size_t element_size;
    VectorFreeFunction free_func;
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static bool vector_resize(Vector *vector, size_t new_capacity) {
    if (new_capacity < vector->size) {
        return false; // Cannot shrink below current size
    }
    
    void *new_data = mem_realloc(vector->data, new_capacity * vector->element_size);
    if (!new_data) {
        return false;
    }
    
    vector->data = new_data;
    vector->capacity = new_capacity;
    return true;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

Vector* vector_create(size_t element_size) {
    Vector *vector = mem_alloc(sizeof(Vector));
    if (!vector) return NULL;
    
    vector->element_size = element_size;
    vector->size = 0;
    vector->capacity = 0;
    vector->data = NULL;
    vector->free_func = NULL;
    
    // Start with capacity for 16 elements
    if (!vector_resize(vector, 16)) {
        mem_free(vector);
        return NULL;
    }
    
    return vector;
}

Vector* vector_create_with_capacity(size_t element_size, size_t capacity) {
    Vector *vector = mem_alloc(sizeof(Vector));
    if (!vector) return NULL;
    
    vector->element_size = element_size;
    vector->size = 0;
    vector->capacity = capacity;
    vector->free_func = NULL;
    
    vector->data = mem_alloc(capacity * element_size);
    if (!vector->data) {
        mem_free(vector);
        return NULL;
    }
    
    return vector;
}

void vector_destroy(Vector *vector) {
    if (!vector) return;
    
    if (vector->free_func) {
        for (size_t i = 0; i < vector->size; i++) {
            vector->free_func(vector_at(vector, i));
        }
    }
    
    if (vector->data) mem_free(vector->data);
    mem_free(vector);
}

void vector_destroy_deep(Vector *vector, VectorFreeFunction free_func) {
    if (!vector) return;
    
    for (size_t i = 0; i < vector->size; i++) {
        free_func(vector_at(vector, i));
    }
    
    if (vector->data) mem_free(vector->data);
    mem_free(vector);
}

void* vector_at(Vector *vector, size_t index) {
    if (!vector || index >= vector->size) return NULL;
    return (char*)vector->data + index * vector->element_size;
}

void vector_set(Vector *vector, size_t index, const void *element) {
    if (!vector || index >= vector->size) return;
    
    if (vector->free_func) {
        vector->free_func(vector_at(vector, index));
    }
    
    void *dest = (char*)vector->data + index * vector->element_size;
    memcpy(dest, element, vector->element_size);
}

void vector_append(Vector *vector, const void *element) {
    if (!vector) return;
    
    if (vector->size >= vector->capacity) {
        size_t new_capacity = vector->capacity * 2;
        if (!vector_resize(vector, new_capacity)) {
            return;
        }
    }
    
    void *dest = (char*)vector->data + vector->size * vector->element_size;
    memcpy(dest, element, vector->element_size);
    vector->size++;
}

void vector_insert(Vector *vector, size_t index, const void *element) {
    if (!vector || index > vector->size) return;
    
    if (index == vector->size) {
        vector_append(vector, element);
        return;
    }
    
    if (vector->size >= vector->capacity) {
        size_t new_capacity = vector->capacity * 2;
        if (!vector_resize(vector, new_capacity)) {
            return;
        }
    }
    
    // Shift elements
    void *src = (char*)vector->data + index * vector->element_size;
    void *dest = (char*)src + vector->element_size;
    size_t count = vector->size - index;
    memmove(dest, src, count * vector->element_size);
    
    // Insert new element
    memcpy(src, element, vector->element_size);
    vector->size++;
}

void vector_remove(Vector *vector, size_t index) {
    if (!vector || index >= vector->size) return;
    
    if (vector->free_func) {
        vector->free_func(vector_at(vector, index));
    }
    
    if (index < vector->size - 1) {
        void *dest = (char*)vector->data + index * vector->element_size;
        void *src = (char*)dest + vector->element_size;
        size_t count = vector->size - index - 1;
        memmove(dest, src, count * vector->element_size);
    }
    
    vector->size--;
}

void vector_clear(Vector *vector) {
    if (!vector) return;
    
    if (vector->free_func) {
        for (size_t i = 0; i < vector->size; i++) {
            vector->free_func(vector_at(vector, i));
        }
    }
    
    vector->size = 0;
}

size_t vector_size(Vector *vector) {
    return vector ? vector->size : 0;
}

size_t vector_capacity(Vector *vector) {
    return vector ? vector->capacity : 0;
}

bool vector_reserve(Vector *vector, size_t capacity) {
    if (!vector || capacity <= vector->capacity) return true;
    return vector_resize(vector, capacity);
}

void vector_shrink_to_fit(Vector *vector) {
    if (!vector || vector->size == vector->capacity) return;
    vector_resize(vector, vector->size);
}

Vector* vector_dup(Vector *vector) {
    if (!vector) return NULL;
    
    Vector *copy = vector_create(vector->element_size);
    if (!copy) return NULL;
    
    if (!vector_reserve(copy, vector->size)) {
        vector_destroy(copy);
        return NULL;
    }
    
    memcpy(copy->data, vector->data, vector->size * vector->element_size);
    copy->size = vector->size;
    copy->free_func = vector->free_func;
    
    return copy;
}

void vector_sort(Vector *vector, VectorCompareFunction compare) {
    if (!vector || !compare || vector->size < 2) return;
    
    qsort(vector->data, vector->size, vector->element_size, compare);
}

void* vector_bsearch(Vector *vector, const void *key, VectorCompareFunction compare) {
    if (!vector || !compare) return NULL;
    return bsearch(key, vector->data, vector->size, vector->element_size, compare);
}

void vector_push(Vector *vector, const void *element) {
    vector_append(vector, element);
}

void* vector_pop(Vector *vector) {
    if (!vector || vector->size == 0) return NULL;
    
    void *element = vector_at(vector, vector->size - 1);
    void *copy = mem_alloc(vector->element_size);
    if (copy) {
        memcpy(copy, element, vector->element_size);
        vector->size--;
    }
    return copy;
}

void vector_iterate(Vector *vector, VectorIterCallback callback, void *user_data) {
    if (!vector || !callback) return;
    
    for (size_t i = 0; i < vector->size; i++) {
        callback(vector_at(vector, i), user_data);
    }
}

void vector_set_free_function(Vector *vector, VectorFreeFunction free_func) {
    if (!vector) return;
    vector->free_func = free_func;
}
