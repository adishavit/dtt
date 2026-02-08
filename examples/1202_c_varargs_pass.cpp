// 1202_c_varargs_pass.cpp
// Tests: Safe C-style variadic function (varargs) usage patterns
// DTT should allow these — all arguments are properly initialized
//
// C varargs use the ellipsis (...) in function declarations.
// DTT tracks that arguments passed to the ellipsis are initialized.

#include <cstdarg>

// =============================================================================
// SECTION 1: Basic C varargs with initialized arguments
// =============================================================================

int printf(const char*, ...);
int sprintf(char*, const char*, ...);
int snprintf(char*, unsigned long, const char*, ...);

// Variant 1.1: printf with initialized int
void variant1_printf_int() {
    int x = 42;
    printf("%d", x);  // OK: x is initialized
}

// Variant 1.2: printf with initialized double
void variant2_printf_double() {
    double d = 3.14;
    printf("%f", d);  // OK: d is initialized
}

// Variant 1.3: printf with multiple initialized args
void variant3_printf_multi() {
    int a = 1, b = 2, c = 3;
    printf("%d %d %d", a, b, c);  // OK: all initialized
}

// Variant 1.4: printf with string literal (always safe)
void variant4_printf_literal() {
    printf("hello %s", "world");  // OK: literals are always initialized
}

// Variant 1.5: printf with char
void variant5_printf_char() {
    char ch = 'A';
    printf("%c", ch);  // OK: ch is initialized
}

// =============================================================================
// SECTION 2: va_list processing with initialized data
// =============================================================================

// Variant 2.1: custom varargs sum function
int sum(int count, ...) {
    va_list args;
    va_start(args, count);
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += va_arg(args, int);
    }
    va_end(args);
    return total;
}

void variant6_custom_varargs() {
    int a = 10, b = 20, c = 30;
    int result = sum(3, a, b, c);  // OK: all initialized
    (void)result;
}

// Variant 2.2: varargs with computed values
void variant7_computed_args() {
    int x = 5;
    int y = x * 2;
    printf("%d %d", x, y);  // OK: both initialized via computation
}

// Variant 2.3: varargs with function return values
int get_value() { return 42; }

void variant8_return_value_arg() {
    printf("%d", get_value());  // OK: return value is initialized
}

// =============================================================================
// SECTION 3: Varargs with pointer arguments
// =============================================================================

// Variant 3.1: initialized pointer to varargs
void variant9_pointer_arg() {
    int x = 42;
    int* p = &x;
    printf("%p", (void*)p);  // OK: p is initialized
}

// Variant 3.2: array decay to pointer in varargs
void variant10_array_arg() {
    char buf[32] = "hello";
    printf("%s", buf);  // OK: buf is initialized
}

// Variant 3.3: sprintf with initialized buffer
void variant11_sprintf() {
    char buf[64];
    int n = 42;
    sprintf(buf, "%d", n);  // OK: n is initialized, buf is output
}

// =============================================================================
// SECTION 4: Varargs with struct members
// =============================================================================

struct Point {
    int x;
    int y;
};

// Variant 4.1: initialized struct member to varargs
void variant12_struct_member() {
    Point p;
    p.x = 10;
    p.y = 20;
    printf("%d %d", p.x, p.y);  // OK: both members initialized
}

// Variant 4.2: aggregate-initialized struct
void variant13_aggregate_init() {
    Point p = {5, 10};
    printf("(%d, %d)", p.x, p.y);  // OK: aggregate init
}

// =============================================================================
// SECTION 5: Varargs in conditional contexts
// =============================================================================

// Variant 5.1: varargs after initialization in both branches
void variant14_conditional(bool cond) {
    int x;
    if (cond) {
        x = 1;
    } else {
        x = 2;
    }
    printf("%d", x);  // OK: initialized in both branches
}

// Variant 5.2: varargs with ternary-initialized value
void variant15_ternary() {
    int x = 0;
    int y = x > 0 ? 1 : -1;
    printf("%d", y);  // OK: y initialized via ternary
}

// =============================================================================
// SECTION 6: Mixed varargs with many argument types
// =============================================================================

// Variant 6.1: many different types
void variant16_mixed_types() {
    int i = 1;
    long l = 2L;
    double d = 3.0;
    char c = '4';
    const char* s = "five";
    printf("%d %ld %f %c %s", i, l, d, c, s);  // OK: all initialized
}

// Variant 6.2: varargs with cast
void variant17_cast_arg() {
    int x = 255;
    printf("%u", (unsigned)x);  // OK: x initialized, cast is fine
}

// Variant 6.3: varargs with sizeof (doesn't evaluate)
void variant18_sizeof_arg() {
    printf("%zu", sizeof(int));  // OK: sizeof is compile-time
}
