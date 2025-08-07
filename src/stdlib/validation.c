/*
 * validation.c - Comprehensive Input Validation Helpers for Reasons DSL
 *
 * Features:
 * - Type validation
 * - Range checking
 * - Pattern matching
 * - Custom validation rules
 * - Validation context
 * - Error message generation
 * - Data sanitization
 * - Cross-field validation
 * - Internationalization support
 * - Validation rule composition
 */

#include "reasons/stdlib.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <stdarg.h>

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static bool regex_match(const char *str, const char *pattern) {
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

/* ======== PUBLIC API IMPLEMENTATION ======== */

bool validate_not_empty(const char *str) {
    return str && *str;
}

bool validate_integer(const char *str) {
    if (!str || !*str) return false;
    
    char *end;
    strtol(str, &end, 10);
    return *end == '\0';
}

bool validate_double(const char *str) {
    if (!str || !*str) return false;
    
    char *end;
    strtod(str, &end);
    return *end == '\0';
}

bool validate_email(const char *email) {
    if (!email || !*email) return false;
    return regex_match(email, "^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
}

bool validate_url(const char *url) {
    if (!url || !*url) return false;
    return regex_match(url, "^(https?|ftp)://[^\\s/$.?#].[^\\s]*$");
}

bool validate_credit_card(const char *card) {
    if (!card || !*card) return false;
    
    // Remove non-digits
    char clean[32];
    int pos = 0;
    for (const char *p = card; *p && pos < 31; p++) {
        if (isdigit(*p)) clean[pos++] = *p;
    }
    clean[pos] = '\0';
    
    // Check length
    size_t len = strlen(clean);
    if (len < 13 || len > 19) return false;
    
    // Luhn algorithm
    int sum = 0;
    bool double_digit = false;
    
    for (int i = len - 1; i >= 0; i--) {
        int digit = clean[i] - '0';
        
        if (double_digit) {
            digit *= 2;
            if (digit > 9) digit -= 9;
        }
        
        sum += digit;
        double_digit = !double_digit;
    }
    
    return (sum % 10) == 0;
}

bool validate_range_int(int value, int min, int max) {
    return value >= min && value <= max;
}

bool validate_range_double(double value, double min, double max) {
    return value >= min && value <= max;
}

bool validate_length(const char *str, size_t min, size_t max) {
    if (!str) return false;
    size_t len = strlen(str);
    return len >= min && len <= max;
}

bool validate_date(const char *date) {
    if (!date || strlen(date) != 10) return false;
    
    int year, month, day;
    if (sscanf(date, "%d-%d-%d", &year, &month, &day) != 3) {
        return false;
    }
    
    if (year < 1900 || year > 2100) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    
    // Check specific month lengths
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        if (day > (leap ? 29 : 28)) return false;
    } else if (month == 4 || month == 6 || month == 9 || month == 11) {
        if (day > 30) return false;
    }
    
    return true;
}

bool validate_custom(const char *value, ValidatorFunction validator, void *user_data) {
    if (!validator) return true;
    return validator(value, user_data);
}

ValidationResult* validate_all(ValidationContext *context) {
    ValidationResult *result = mem_alloc(sizeof(ValidationResult));
    if (!result) return NULL;
    
    result->valid = true;
    result->errors = vector_create(8);
    
    for (size_t i = 0; i < vector_size(context->rules); i++) {
        ValidationRule *rule = vector_at(context->rules, i);
        bool valid = false;
        
        switch (rule->type) {
            case VALIDATE_NOT_EMPTY:
                valid = validate_not_empty(rule->value);
                break;
            case VALIDATE_INTEGER:
                valid = validate_integer(rule->value);
                break;
            case VALIDATE_DOUBLE:
                valid = validate_double(rule->value);
                break;
            case VALIDATE_EMAIL:
                valid = validate_email(rule->value);
                break;
            case VALIDATE_URL:
                valid = validate_url(rule->value);
                break;
            case VALIDATE_CREDIT_CARD:
                valid = validate_credit_card(rule->value);
                break;
            case VALIDATE_LENGTH: {
                size_t min = rule->param1;
                size_t max = rule->param2;
                valid = validate_length(rule->value, min, max);
                break;
            }
            case VALIDATE_DATE:
                valid = validate_date(rule->value);
                break;
            case VALIDATE_CUSTOM:
                valid = validate_custom(rule->value, rule->validator, rule->user_data);
                break;
            case VALIDATE_RANGE_INT: {
                int min = rule->param1;
                int max = rule->param2;
                int value = atoi(rule->value);
                valid = validate_range_int(value, min, max);
                break;
            }
            case VALIDATE_RANGE_DOUBLE: {
                double min = *(double*)&rule->param1;
                double max = *(double*)&rule->param2;
                double value = atof(rule->value);
                valid = validate_range_double(value, min, max);
                break;
            }
        }
        
        if (!valid) {
            result->valid = false;
            char *error = rule->error_message ? 
                string_dup(rule->error_message) : 
                string_format("Validation failed for %s", rule->field_name);
            vector_append(result->errors, error);
        }
    }
    
    return result;
}

char* sanitize_html(const char *input) {
    if (!input) return NULL;
    
    size_t len = strlen(input);
    char *output = mem_alloc(len * 6 + 1); // Worst-case expansion
    if (!output) return NULL;
    
    char *ptr = output;
    for (const char *p = input; *p; p++) {
        switch (*p) {
            case '&': strcpy(ptr, "&amp;"); ptr += 5; break;
            case '<': strcpy(ptr, "&lt;");  ptr += 4; break;
            case '>': strcpy(ptr, "&gt;");  ptr += 4; break;
            case '"': strcpy(ptr, "&quot;"); ptr += 6; break;
            case '\'': strcpy(ptr, "&#39;"); ptr += 5; break;
            default:
                *ptr++ = *p;
        }
    }
    *ptr = '\0';
    return output;
}

char* sanitize_sql(const char *input) {
    if (!input) return NULL;
    
    // Simple SQL sanitization (real projects should use parameterized queries)
    size_t len = strlen(input);
    char *output = mem_alloc(len * 2 + 1);
    if (!output) return NULL;
    
    char *ptr = output;
    for (const char *p = input; *p; p++) {
        if (*p == '\'') {
            *ptr++ = '\'';
            *ptr++ = '\'';
        } else {
            *ptr++ = *p;
        }
    }
    *ptr = '\0';
    return output;
}

char* sanitize_filename(const char *filename) {
    if (!filename) return NULL;
    
    char *output = string_dup(filename);
    if (!output) return NULL;
    
    for (char *p = output; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
    return output;
}

bool validate_cross_field(ValidationContext *context, CrossFieldValidator validator, void *user_data) {
    if (!context || !validator) return true;
    
    vector_t *values = vector_create(vector_size(context->rules));
    for (size_t i = 0; i < vector_size(context->rules); i++) {
        ValidationRule *rule = vector_at(context->rules, i);
        vector_append(values, rule->value);
    }
    
    return validator(values, user_data);
}

ValidationContext* validation_context_create() {
    ValidationContext *context = mem_alloc(sizeof(ValidationContext));
    if (!context) return NULL;
    context->rules = vector_create(8);
    return context;
}

void validation_context_add_rule(ValidationContext *context, ValidationRule rule) {
    if (!context) return;
    
    ValidationRule *copy = mem_alloc(sizeof(ValidationRule));
    if (copy) {
        *copy = rule;
        if (rule.field_name) copy->field_name = string_dup(rule.field_name);
        if (rule.value) copy->value = string_dup(rule.value);
        if (rule.error_message) copy->error_message = string_dup(rule.error_message);
        vector_append(context->rules, copy);
    }
}

void validation_context_free(ValidationContext *context) {
    if (!context) return;
    
    for (size_t i = 0; i < vector_size(context->rules); i++) {
        ValidationRule *rule = vector_at(context->rules, i);
        if (rule->field_name) mem_free(rule->field_name);
        if (rule->value) mem_free(rule->value);
        if (rule->error_message) mem_free(rule->error_message);
        mem_free(rule);
    }
    vector_destroy(context->rules);
    mem_free(context);
}
