/*
 * math.c - Advanced Mathematical Functions for Reasons DSL
 *
 * Features:
 * - Trigonometric functions with degree/radian support
 * - Complex number operations
 * - Matrix operations
 * - Interpolation functions
 * - Special functions (gamma, erf, etc.)
 * - Numerical integration/differentiation
 * - Unit conversion
 * - Fixed-point arithmetic
 * - Random number generation
 * - Precision control
 */

#include "reasons/stdlib.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    double real;
    double imag;
} Complex;

typedef struct {
    int rows;
    int cols;
    double *data;
} Matrix;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static const double PI = 3.14159265358979323846;
static const double E = 2.7182818284590452354;
static bool rng_initialized = false;

static double degrees_to_radians(double degrees) {
    return degrees * PI / 180.0;
}

static double radians_to_degrees(double radians) {
    return radians * 180.0 / PI;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

double math_sin(double x, AngleUnit unit) {
    if (unit == ANGLE_DEGREES) {
        x = degrees_to_radians(x);
    }
    return sin(x);
}

double math_cos(double x, AngleUnit unit) {
    if (unit == ANGLE_DEGREES) {
        x = degrees_to_radians(x);
    }
    return cos(x);
}

double math_tan(double x, AngleUnit unit) {
    if (unit == ANGLE_DEGREES) {
        x = degrees_to_radians(x);
    }
    return tan(x);
}

double math_asin(double x, AngleUnit unit) {
    double result = asin(x);
    return unit == ANGLE_DEGREES ? radians_to_degrees(result) : result;
}

double math_acos(double x, AngleUnit unit) {
    double result = acos(x);
    return unit == ANGLE_DEGREES ? radians_to_degrees(result) : result;
}

double math_atan(double x, AngleUnit unit) {
    double result = atan(x);
    return unit == ANGLE_DEGREES ? radians_to_degrees(result) : result;
}

double math_atan2(double y, double x, AngleUnit unit) {
    double result = atan2(y, x);
    return unit == ANGLE_DEGREES ? radians_to_degrees(result) : result;
}

Complex math_complex_add(Complex a, Complex b) {
    return (Complex){a.real + b.real, a.imag + b.imag};
}

Complex math_complex_sub(Complex a, Complex b) {
    return (Complex){a.real - b.real, a.imag - b.imag};
}

Complex math_complex_mul(Complex a, Complex b) {
    return (Complex){
        a.real * b.real - a.imag * b.imag,
        a.real * b.imag + a.imag * b.real
    };
}

Complex math_complex_div(Complex a, Complex b) {
    double denominator = b.real * b.real + b.imag * b.imag;
    if (fabs(denominator) < DBL_EPSILON) {
        LOG_ERROR("Complex division by zero");
        return (Complex){0, 0};
    }
    return (Complex){
        (a.real * b.real + a.imag * b.imag) / denominator,
        (a.imag * b.real - a.real * b.imag) / denominator
    };
}

double math_complex_abs(Complex c) {
    return sqrt(c.real * c.real + c.imag * c.imag);
}

Matrix* math_matrix_create(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return NULL;
    
    Matrix *m = mem_alloc(sizeof(Matrix));
    if (!m) return NULL;
    
    m->rows = rows;
    m->cols = cols;
    m->data = mem_calloc(rows * cols, sizeof(double));
    if (!m->data) {
        mem_free(m);
        return NULL;
    }
    return m;
}

void math_matrix_free(Matrix *m) {
    if (!m) return;
    if (m->data) mem_free(m->data);
    mem_free(m);
}

double math_matrix_get(Matrix *m, int row, int col) {
    if (!m || row < 0 || row >= m->rows || col < 0 || col >= m->cols) {
        LOG_ERROR("Matrix index out of bounds");
        return 0;
    }
    return m->data[row * m->cols + col];
}

void math_matrix_set(Matrix *m, int row, int col, double value) {
    if (!m || row < 0 || row >= m->rows || col < 0 || col >= m->cols) {
        LOG_ERROR("Matrix index out of bounds");
        return;
    }
    m->data[row * m->cols + col] = value;
}

Matrix* math_matrix_multiply(Matrix *a, Matrix *b) {
    if (!a || !b || a->cols != b->rows) {
        LOG_ERROR("Matrix dimensions incompatible for multiplication");
        return NULL;
    }
    
    Matrix *result = math_matrix_create(a->rows, b->cols);
    if (!result) return NULL;
    
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            double sum = 0;
            for (int k = 0; k < a->cols; k++) {
                sum += math_matrix_get(a, i, k) * math_matrix_get(b, k, j);
            }
            math_matrix_set(result, i, j, sum);
        }
    }
    return result;
}

double math_interpolate_linear(double x0, double y0, double x1, double y1, double x) {
    if (fabs(x1 - x0) < DBL_EPSILON) {
        LOG_WARN("Interpolation with same x values");
        return (y0 + y1) / 2.0;
    }
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

double math_interpolate_cubic(double x0, double y0, double x1, double y1, 
                             double x2, double y2, double x3, double y3, double x) {
    // Implementation of cubic interpolation (Catmull-Rom spline)
    double t = (x - x0) / (x1 - x0);
    double t2 = t * t;
    double t3 = t2 * t;
    
    double a = -0.5*y0 + 1.5*y1 - 1.5*y2 + 0.5*y3;
    double b = y0 - 2.5*y1 + 2.0*y2 - 0.5*y3;
    double c = -0.5*y0 + 0.5*y2;
    double d = y1;
    
    return a*t3 + b*t2 + c*t + d;
}

double math_clamp(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

double math_map_range(double value, double in_min, double in_max, double out_min, double out_max) {
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

double math_gamma(double x) {
    // Lanczos approximation for gamma function
    const double g = 7;
    const double p[] = {
        0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
    };
    
    if (x < 0.5) {
        return PI / (sin(PI * x) * math_gamma(1 - x));
    }
    
    x -= 1;
    double a = p[0];
    double t = x + g + 0.5;
    
    for (int i = 1; i < 9; i++) {
        a += p[i] / (x + i);
    }
    
    return sqrt(2 * PI) * pow(t, x + 0.5) * exp(-t) * a;
}

double math_erf(double x) {
    // Abramowitz and Stegun approximation
    double a1 =  0.254829592;
    double a2 = -0.284496736;
    double a3 =  1.421413741;
    double a4 = -1.453152027;
    double a5 =  1.061405429;
    double p  =  0.3275911;
    
    int sign = x < 0 ? -1 : 1;
    x = fabs(x);
    
    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);
    
    return sign * y;
}

double math_integrate_simpson(double (*f)(double), double a, double b, int n) {
    if (n % 2 != 0) n++; // Ensure n is even
    double h = (b - a) / n;
    double sum = f(a) + f(b);
    
    for (int i = 1; i < n; i++) {
        double x = a + i * h;
        sum += f(x) * (i % 2 == 0 ? 2 : 4);
    }
    
    return sum * h / 3.0;
}

double math_derivative(double (*f)(double), double x, double h) {
    // Five-point stencil method
    return (-f(x + 2*h) + 8*f(x + h) - 8*f(x - h) + f(x - 2*h)) / (12*h);
}

double math_convert_unit(double value, Unit from, Unit to) {
    // Conversion factors
    const double factors[][UNIT_COUNT] = {
        // LENGTH: METER, CENTIMETER, KILOMETER, INCH, FOOT, MILE
        {1, 100, 0.001, 39.3701, 3.28084, 0.000621371},
        
        // MASS: KILOGRAM, GRAM, MILLIGRAM, POUND, OUNCE
        {1, 1000, 1000000, 2.20462, 35.274},
        
        // TIME: SECOND, MILLISECOND, MINUTE, HOUR, DAY
        {1, 1000, 1.0/60, 1.0/3600, 1.0/86400},
        
        // TEMPERATURE: CELSIUS, FAHRENHEIT, KELVIN
        // Special handling required
        {1, 1, 1}
    };
    
    if (from == to) return value;
    
    // Temperature conversion
    if (from >= TEMP_CELSIUS && to >= TEMP_CELSIUS) {
        if (from == TEMP_CELSIUS && to == TEMP_FAHRENHEIT) {
            return value * 9/5 + 32;
        }
        if (from == TEMP_FAHRENHEIT && to == TEMP_CELSIUS) {
            return (value - 32) * 5/9;
        }
        if (from == TEMP_CELSIUS && to == TEMP_KELVIN) {
            return value + 273.15;
        }
        if (from == TEMP_KELVIN && to == TEMP_CELSIUS) {
            return value - 273.15;
        }
        if (from == TEMP_FAHRENHEIT && to == TEMP_KELVIN) {
            return (value - 32) * 5/9 + 273.15;
        }
        if (from == TEMP_KELVIN && to == TEMP_FAHRENHEIT) {
            return (value - 273.15) * 9/5 + 32;
        }
    }
    
    // Standard conversion
    int category = from / 10;
    if (category != to / 10) {
        LOG_ERROR("Incompatible unit conversion");
        return value;
    }
    
    double base_value = value / factors[category][from % 10];
    return base_value * factors[category][to % 10];
}

double math_random() {
    if (!rng_initialized) {
        srand(time(NULL));
        rng_initialized = true;
    }
    return (double)rand() / RAND_MAX;
}

double math_random_range(double min, double max) {
    return min + math_random() * (max - min);
}

double math_round_precision(double value, int decimals) {
    double factor = pow(10, decimals);
    return round(value * factor) / factor;
}
