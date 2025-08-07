/*
 * string_utils.c - Comprehensive String Utilities for Reasons DSL
 *
 * Features:
 * - UTF-8 aware functions
 * - String formatting
 * - String splitting/joining
 * - Case conversion
 * - Trimming
 * - Search and replace
 * - String encoding conversion
 * - String hashing
 * - String interpolation
 * - Secure string handling
 */

#include "utils/string_utils.h"
#include "utils/memory.h"
#include "utils/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <wchar.h>
#include <locale.h>
#include <iconv.h>
#include <errno.h>

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static size_t utf8_char_len(const char *str) {
    unsigned char c = (unsigned char)str[0];
    if (c < 0x80) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    if (c < 0xF8) return 4;
    return 1; // Invalid, but safe
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

char* string_dup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *copy = mem_alloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

char* string_ndup(const char *str, size_t n) {
    if (!str) return NULL;
    size_t len = strnlen(str, n);
    char *copy = mem_alloc(len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

char* string_format(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // Determine required size
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (size < 0) {
        va_end(args);
        return NULL;
    }
    
    char *buffer = mem_alloc(size + 1);
    if (!buffer) {
        va_end(args);
        return NULL;
    }
    
    vsnprintf(buffer, size + 1, format, args);
    va_end(args);
    
    return buffer;
}

char* string_vformat(const char *format, va_list args) {
    // Determine required size
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (size < 0) return NULL;
    
    char *buffer = mem_alloc(size + 1);
    if (!buffer) return NULL;
    
    vsnprintf(buffer, size + 1, format, args);
    return buffer;
}

char* string_trim(const char *str) {
    if (!str) return NULL;
    
    // Find start of non-whitespace
    const char *start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    
    // All whitespace
    if (*start == '\0') return string_dup("");
    
    // Find end of non-whitespace
    const char *end = str + strlen(str) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    size_t len = end - start + 1;
    char *result = mem_alloc(len + 1);
    if (result) {
        memcpy(result, start, len);
        result[len] = '\0';
    }
    return result;
}

vector_t* string_split(const char *str, const char *delimiter) {
    vector_t *parts = vector_create(8);
    if (!str || !delimiter) return parts;
    
    size_t delim_len = strlen(delimiter);
    if (delim_len == 0) {
        vector_append(parts, string_dup(str));
        return parts;
    }
    
    const char *start = str;
    const char *end;
    while ((end = strstr(start, delimiter)) != NULL) {
        size_t len = end - start;
        char *part = mem_alloc(len + 1);
        if (part) {
            memcpy(part, start, len);
            part[len] = '\0';
            vector_append(parts, part);
        }
        start = end + delim_len;
    }
    
    // Add remaining part
    if (*start) {
        vector_append(parts, string_dup(start));
    }
    
    return parts;
}

char* string_join(vector_t *parts, const char *separator) {
    if (!parts || vector_size(parts) == 0) return string_dup("");
    
    size_t sep_len = separator ? strlen(separator) : 0;
    
    // Calculate total length
    size_t total_len = 0;
    for (size_t i = 0; i < vector_size(parts); i++) {
        total_len += strlen(vector_at(parts, i));
    }
    total_len += sep_len * (vector_size(parts) - 1);
    
    char *result = mem_alloc(total_len + 1);
    if (!result) return NULL;
    
    char *ptr = result;
    for (size_t i = 0; i < vector_size(parts); i++) {
        const char *part = vector_at(parts, i);
        size_t part_len = strlen(part);
        memcpy(ptr, part, part_len);
        ptr += part_len;
        
        if (i < vector_size(parts) - 1 && sep_len > 0) {
            memcpy(ptr, separator, sep_len);
            ptr += sep_len;
        }
    }
    *ptr = '\0';
    return result;
}

char* string_replace(const char *str, const char *old_sub, const char *new_sub) {
    if (!str || !old_sub || !*old_sub) return string_dup(str);
    
    size_t old_len = strlen(old_sub);
    size_t new_len = new_sub ? strlen(new_sub) : 0;
    
    // Count occurrences
    size_t count = 0;
    const char *p = str;
    while ((p = strstr(p, old_sub)) != NULL) {
        count++;
        p += old_len;
    }
    
    // Allocate result buffer
    size_t result_len = strlen(str) + count * (new_len - old_len);
    char *result = mem_alloc(result_len + 1);
    if (!result) return NULL;
    
    // Perform replacement
    char *out = result;
    p = str;
    while (*p) {
        if (strstr(p, old_sub) == p) {
            memcpy(out, new_sub, new_len);
            out += new_len;
            p += old_len;
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';
    return result;
}

char* string_to_lower(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *result = mem_alloc(len + 1);
    if (!result) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        result[i] = tolower((unsigned char)str[i]);
    }
    result[len] = '\0';
    return result;
}

char* string_to_upper(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *result = mem_alloc(len + 1);
    if (!result) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        result[i] = toupper((unsigned char)str[i]);
    }
    result[len] = '\0';
    return result;
}

char* string_convert_encoding(const char *str, const char *from_enc, const char *to_enc) {
    iconv_t cd = iconv_open(to_enc, from_enc);
    if (cd == (iconv_t)-1) {
        return NULL;
    }
    
    size_t in_bytes = strlen(str);
    size_t out_bytes = in_bytes * 4; // Safe buffer size
    char *out_buf = mem_alloc(out_bytes + 1);
    if (!out_buf) {
        iconv_close(cd);
        return NULL;
    }
    
    char *in_ptr = (char*)str;
    char *out_ptr = out_buf;
    size_t in_left = in_bytes;
    size_t out_left = out_bytes;
    
    if (iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left) == (size_t)-1) {
        iconv_close(cd);
        mem_free(out_buf);
        return NULL;
    }
    
    iconv_close(cd);
    *out_ptr = '\0';
    return out_buf;
}

uint32_t string_hash(const char *str) {
    // FNV-1a hash
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 16777619;
    }
    return hash;
}

char* string_escape(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *result = mem_alloc(len * 4 + 1); // Worst-case buffer
    if (!result) return NULL;
    
    char *ptr = result;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '\n': *ptr++ = '\\'; *ptr++ = 'n'; break;
            case '\r': *ptr++ = '\\'; *ptr++ = 'r'; break;
            case '\t': *ptr++ = '\\'; *ptr++ = 't'; break;
            case '\b': *ptr++ = '\\'; *ptr++ = 'b'; break;
            case '\f': *ptr++ = '\\'; *ptr++ = 'f'; break;
            case '\"': *ptr++ = '\\'; *ptr++ = '\"'; break;
            case '\\': *ptr++ = '\\'; *ptr++ = '\\'; break;
            default:
                if ((unsigned char)*p < 0x20) {
                    ptr += sprintf(ptr, "\\x%02x", (unsigned char)*p);
                } else {
                    *ptr++ = *p;
                }
        }
    }
    *ptr = '\0';
    return result;
}

size_t string_utf8_len(const char *str) {
    size_t len = 0;
    while (*str) {
        size_t char_len = utf8_char_len(str);
        str += char_len;
        len++;
    }
    return len;
}
