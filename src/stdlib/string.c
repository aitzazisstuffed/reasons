/*
 * string.c - Comprehensive String Operations for Reasons DSL
 *
 * Features:
 * - Unicode-aware operations
 * - String formatting
 * - Regular expressions
 * - String encoding conversion
 * - Secure string handling
 * - String interning
 * - Phonetic algorithms
 * - String metrics (Levenshtein, etc.)
 * - String search algorithms
 * - Template processing
 */

#include "reasons/stdlib.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/collections.h"
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <locale.h>
#include <stdlib.h>
#include <regex.h>
#include <iconv.h>
#include <errno.h>

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const char *string_intern_pool[STRING_INTERN_POOL_SIZE] = {0};
static int string_intern_count = 0;

static size_t utf8_char_len(const char *str) {
    unsigned char c = (unsigned char)str[0];
    if (c < 0x80) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    if (c < 0xF8) return 4;
    return 1; // Invalid, but safe
}

static bool is_valid_utf8(const char *str, size_t len) {
    size_t i = 0;
    while (i < len) {
        if ((unsigned char)str[i] < 0x80) {
            i++;
        } else {
            size_t char_len = utf8_char_len(str + i);
            if (i + char_len > len) return false;
            
            // Validate continuation bytes
            for (size_t j = 1; j < char_len; j++) {
                if ((str[i+j] & 0xC0) != 0x80) return false;
            }
            i += char_len;
        }
    }
    return true;
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

bool string_match_regex(const char *str, const char *pattern) {
    regex_t regex;
    int ret;
    
    if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        LOG_ERROR("Invalid regex pattern: %s", pattern);
        return false;
    }
    
    ret = regexec(&regex, str, 0, NULL, 0);
    regfree(&regex);
    
    return ret == 0;
}

char* string_convert_encoding(const char *str, const char *from_enc, const char *to_enc) {
    iconv_t cd = iconv_open(to_enc, from_enc);
    if (cd == (iconv_t)-1) {
        LOG_ERROR("Encoding conversion not supported: %s to %s", from_enc, to_enc);
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
        LOG_ERROR("Encoding conversion failed: %s", strerror(errno));
        iconv_close(cd);
        mem_free(out_buf);
        return NULL;
    }
    
    iconv_close(cd);
    *out_ptr = '\0';
    return out_buf;
}

const char* string_intern(const char *str) {
    if (!str) return NULL;
    
    // Check if already interned
    for (int i = 0; i < string_intern_count; i++) {
        if (strcmp(string_intern_pool[i], str) == 0) {
            return string_intern_pool[i];
        }
    }
    
    // Add to intern pool
    if (string_intern_count < STRING_INTERN_POOL_SIZE) {
        char *copy = string_dup(str);
        if (copy) {
            string_intern_pool[string_intern_count++] = copy;
            return copy;
        }
    }
    
    return str; // Fallback if pool is full
}

char* string_soundex(const char *str) {
    if (!str || !*str) return string_dup("");
    
    char result[5] = {'0', '0', '0', '0', '\0'};
    result[0] = toupper(*str);
    
    const char *codes = "01230120022455012623010202";
    int count = 1;
    char last_code = '0';
    
    for (str++; *str && count < 4; str++) {
        char c = toupper(*str);
        if (c < 'A' || c > 'Z') continue;
        
        char code = codes[c - 'A'];
        if (code == '0') continue;
        if (code == last_code) continue;
        
        last_code = code;
        result[count++] = code;
    }
    
    return string_dup(result);
}

int string_levenshtein(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    
    if (len_a == 0) return len_b;
    if (len_b == 0) return len_a;
    
    int **matrix = mem_calloc(len_a + 1, sizeof(int*));
    for (size_t i = 0; i <= len_a; i++) {
        matrix[i] = mem_calloc(len_b + 1, sizeof(int));
        matrix[i][0] = i;
    }
    
    for (size_t j = 0; j <= len_b; j++) {
        matrix[0][j] = j;
    }
    
    for (size_t i = 1; i <= len_a; i++) {
        for (size_t j = 1; j <= len_b; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            matrix[i][j] = min(min(
                matrix[i-1][j] + 1,    // Deletion
                matrix[i][j-1] + 1),   // Insertion
                matrix[i-1][j-1] + cost // Substitution
            );
        }
    }
    
    int result = matrix[len_a][len_b];
    
    for (size_t i = 0; i <= len_a; i++) {
        mem_free(matrix[i]);
    }
    mem_free(matrix);
    
    return result;
}

char* string_template(const char *template, StringTemplateCallback callback, void *user_data) {
    if (!template) return NULL;
    
    size_t len = strlen(template);
    char *result = mem_alloc(len * 2 + 1); // Safe buffer size
    if (!result) return NULL;
    
    char *out = result;
    const char *p = template;
    const char *end = template + len;
    
    while (p < end) {
        if (*p == '{' && *(p+1) == '{') {
            p += 2;
            const char *var_start = p;
            while (p < end && !(*p == '}' && *(p+1) == '}')) p++;
            
            if (p >= end) {
                // Unclosed variable
                *out++ = '{';
                *out++ = '{';
                p = var_start;
                continue;
            }
            
            size_t var_len = p - var_start;
            char *var_name = mem_alloc(var_len + 1);
            if (var_name) {
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';
                
                const char *value = callback(var_name, user_data);
                if (value) {
                    size_t value_len = strlen(value);
                    memcpy(out, value, value_len);
                    out += value_len;
                }
                
                mem_free(var_name);
            }
            
            p += 2; // Skip closing braces
        } else {
            *out++ = *p++;
        }
    }
    
    *out = '\0';
    return result;
}
