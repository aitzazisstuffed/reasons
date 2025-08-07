/*
 * stats.c - Comprehensive Statistical Functions for Reasons DSL
 *
 * Features:
 * - Descriptive statistics
 * - Probability distributions
 * - Hypothesis testing
 * - Correlation analysis
 * - Regression analysis
 * - Time series analysis
 * - Bayesian statistics
 * - Non-parametric statistics
 * - Statistical tests
 * - Random sampling
 */

#include "reasons/stdlib.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/collections.h"
#include <math.h>
#include <stdlib.h>
#include <float.h>

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static int compare_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

static double normal_pdf(double x, double mean, double stddev) {
    double exponent = exp(-(x - mean) * (x - mean) / (2 * stddev * stddev));
    return exponent / (stddev * sqrt(2 * M_PI));
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

double stats_mean(vector_t *data) {
    if (!data || vector_size(data) == 0) return NAN;
    
    double sum = 0;
    for (size_t i = 0; i < vector_size(data); i++) {
        double value = *(double*)vector_at(data, i);
        sum += value;
    }
    return sum / vector_size(data);
}

double stats_median(vector_t *data) {
    if (!data || vector_size(data) == 0) return NAN;
    
    // Create a sorted copy
    double *sorted = mem_alloc(vector_size(data) * sizeof(double));
    for (size_t i = 0; i < vector_size(data); i++) {
        sorted[i] = *(double*)vector_at(data, i);
    }
    qsort(sorted, vector_size(data), sizeof(double), compare_double);
    
    size_t n = vector_size(data);
    if (n % 2 == 0) {
        return (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    } else {
        return sorted[n/2];
    }
}

double stats_mode(vector_t *data) {
    if (!data || vector_size(data) == 0) return NAN;
    
    // Count occurrences
    HashTable *counts = hashtable_create(vector_size(data), NULL);
    double max_value = 0;
    int max_count = 0;
    
    for (size_t i = 0; i < vector_size(data); i++) {
        double value = *(double*)vector_at(data, i);
        int *count_ptr = hashtable_get(counts, &value, sizeof(value));
        int count = count_ptr ? *count_ptr + 1 : 1;
        
        char key[sizeof(double)];
        memcpy(key, &value, sizeof(double));
        hashtable_set(counts, key, sizeof(double), &count, sizeof(int));
        
        if (count > max_count) {
            max_count = count;
            max_value = value;
        }
    }
    
    hashtable_destroy(counts);
    return max_value;
}

double stats_variance(vector_t *data, bool sample) {
    if (!data || vector_size(data) < 2) return NAN;
    
    double mean = stats_mean(data);
    double sum_sq_diff = 0;
    
    for (size_t i = 0; i < vector_size(data); i++) {
        double value = *(double*)vector_at(data, i);
        double diff = value - mean;
        sum_sq_diff += diff * diff;
    }
    
    int divisor = sample ? vector_size(data) - 1 : vector_size(data);
    return sum_sq_diff / divisor;
}

double stats_stddev(vector_t *data, bool sample) {
    return sqrt(stats_variance(data, sample));
}

double stats_percentile(vector_t *data, double p) {
    if (!data || vector_size(data) == 0 || p < 0 || p > 1) return NAN;
    
    // Create sorted copy
    double *sorted = mem_alloc(vector_size(data) * sizeof(double));
    for (size_t i = 0; i < vector_size(data); i++) {
        sorted[i] = *(double*)vector_at(data, i);
    }
    qsort(sorted, vector_size(data), sizeof(double), compare_double);
    
    // Calculate position
    double n = vector_size(data);
    double pos = p * (n - 1);
    int index = (int)pos;
    double frac = pos - index;
    
    if (index == n - 1) {
        return sorted[index];
    }
    return sorted[index] + frac * (sorted[index+1] - sorted[index]);
}

double stats_correlation(vector_t *x, vector_t *y) {
    if (!x || !y || vector_size(x) != vector_size(y) || vector_size(x) == 0) {
        return NAN;
    }
    
    double sum_x = 0, sum_y = 0;
    double sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    size_t n = vector_size(x);
    
    for (size_t i = 0; i < n; i++) {
        double xi = *(double*)vector_at(x, i);
        double yi = *(double*)vector_at(y, i);
        
        sum_x += xi;
        sum_y += yi;
        sum_xy += xi * yi;
        sum_x2 += xi * xi;
        sum_y2 += yi * yi;
    }
    
    double numerator = n * sum_xy - sum_x * sum_y;
    double denominator = sqrt((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
    
    if (fabs(denominator) < DBL_EPSILON) {
        return 0;
    }
    
    return numerator / denominator;
}

LinearRegression stats_linear_regression(vector_t *x, vector_t *y) {
    LinearRegression result = {NAN, NAN, NAN, NAN};
    if (!x || !y || vector_size(x) != vector_size(y) || vector_size(x) < 2) {
        return result;
    }
    
    double sum_x = 0, sum_y = 0;
    double sum_xy = 0, sum_x2 = 0;
    size_t n = vector_size(x);
    
    for (size_t i = 0; i < n; i++) {
        double xi = *(double*)vector_at(x, i);
        double yi = *(double*)vector_at(y, i);
        
        sum_x += xi;
        sum_y += yi;
        sum_xy += xi * yi;
        sum_x2 += xi * xi;
    }
    
    double numerator = n * sum_xy - sum_x * sum_y;
    double denominator = n * sum_x2 - sum_x * sum_x;
    
    if (fabs(denominator) < DBL_EPSILON) {
        return result;
    }
    
    result.slope = numerator / denominator;
    result.intercept = (sum_y - result.slope * sum_x) / n;
    
    // Calculate R-squared
    double ss_total = 0, ss_res = 0;
    double mean_y = sum_y / n;
    
    for (size_t i = 0; i < n; i++) {
        double xi = *(double*)vector_at(x, i);
        double yi = *(double*)vector_at(y, i);
        double y_pred = result.slope * xi + result.intercept;
        
        ss_total += (yi - mean_y) * (yi - mean_y);
        ss_res += (yi - y_pred) * (yi - y_pred);
    }
    
    result.r_squared = 1.0 - (ss_res / ss_total);
    return result;
}

double stats_normal_cdf(double x, double mean, double stddev) {
    // Abramowitz and Stegun approximation
    double t = (x - mean) / (stddev * sqrt(2));
    double sign = t < 0 ? -1 : 1;
    t = fabs(t);
    
    double a1 =  0.254829592;
    double a2 = -0.284496736;
    double a3 =  1.421413741;
    double a4 = -1.453152027;
    double a5 =  1.061405429;
    double p  =  0.3275911;
    
    double t1 = 1.0 / (1.0 + p * t);
    double erf = 1.0 - (((((a5 * t1 + a4) * t1) + a3) * t1 + a2) * t1 + a1) * t1 * exp(-t * t);
    
    return 0.5 * (1 + sign * erf);
}

double stats_t_test(vector_t *sample1, vector_t *sample2) {
    if (!sample1 || !sample2 || vector_size(sample1) < 2 || vector_size(sample2) < 2) {
        return NAN;
    }
    
    double mean1 = stats_mean(sample1);
    double mean2 = stats_mean(sample2);
    double var1 = stats_variance(sample1, true);
    double var2 = stats_variance(sample2, true);
    
    size_t n1 = vector_size(sample1);
    size_t n2 = vector_size(sample2);
    
    double pooled_var = ((n1 - 1) * var1 + (n2 - 1) * var2) / (n1 + n2 - 2);
    double t = (mean1 - mean2) / sqrt(pooled_var * (1.0/n1 + 1.0/n2));
    
    // Simplified: return t-value, caller can convert to p-value
    return t;
}

vector_t* stats_random_sample(vector_t *population, size_t n) {
    if (!population || n == 0 || n > vector_size(population)) {
        return NULL;
    }
    
    // Fisher-Yates shuffle
    vector_t *copy = vector_dup(population);
    for (size_t i = 0; i < n; i++) {
        size_t j = i + rand() % (vector_size(copy) - i);
        void *tmp = vector_at(copy, i);
        vector_set(copy, i, vector_at(copy, j));
        vector_set(copy, j, tmp);
    }
    
    // Create result with first n elements
    vector_t *result = vector_create(n);
    for (size_t i = 0; i < n; i++) {
        vector_append(result, vector_at(copy, i));
    }
    
    vector_destroy(copy);
    return result;
}

Histogram* stats_histogram(vector_t *data, int bins) {
    if (!data || vector_size(data) == 0 || bins < 1) return NULL;
    
    // Find min and max
    double min_val = DBL_MAX, max_val = -DBL_MAX;
    for (size_t i = 0; i < vector_size(data); i++) {
        double value = *(double*)vector_at(data, i);
        if (value < min_val) min_val = value;
        if (value > max_val) max_val = value;
    }
    
    // Handle constant data
    if (fabs(max_val - min_val) < DBL_EPSILON) {
        min_val -= 0.5;
        max_val += 0.5;
    }
    
    double bin_width = (max_val - min_val) / bins;
    
    Histogram *hist = mem_alloc(sizeof(Histogram));
    if (!hist) return NULL;
    
    hist->min = min_val;
    hist->max = max_val;
    hist->bin_width = bin_width;
    hist->bins = vector_create(bins);
    for (int i = 0; i < bins; i++) {
        int *count = mem_alloc(sizeof(int));
        *count = 0;
        vector_append(hist->bins, count);
    }
    
    // Count values
    for (size_t i = 0; i < vector_size(data); i++) {
        double value = *(double*)vector_at(data, i);
        int bin_index = (value - min_val) / bin_width;
        
        if (bin_index >= bins) bin_index = bins - 1;
        if (bin_index < 0) bin_index = 0;
        
        int *count = vector_at(hist->bins, bin_index);
        (*count)++;
    }
    
    return hist;
}

double stats_normal_distribution(double x, double mean, double stddev) {
    return normal_pdf(x, mean, stddev);
}

double stats_exponential_distribution(double x, double lambda) {
    if (x < 0) return 0;
    return lambda * exp(-lambda * x);
}

double stats_poisson_distribution(int k, double lambda) {
    return exp(-lambda) * pow(lambda, k) / tgamma(k + 1);
}
