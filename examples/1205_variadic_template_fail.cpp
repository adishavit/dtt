// 1205_variadic_template_fail.cpp
// Tests: Unsafe C++ variadic template patterns — DTT should catch these
// Uninitialized values and tracked-property violations through parameter packs

#include <cstddef>
#include <utility>

// =============================================================================
// SECTION 1: Uninitialized scalars through variadic templates
// =============================================================================

template<typename... Args>
void consume(Args... args);

void sink(int x);

// Variant 1.1: uninitialized int through variadic call
void variant1_uninit_variadic() {
    int x;
    consume(x);  // ERROR: x is uninitialized
}

// Variant 1.2: uninitialized among initialized in pack
void variant2_mixed_pack() {
    int a = 1;
    int b;
    int c = 3;
    consume(a, b, c);  // ERROR: b is uninitialized
}

// Variant 1.3: all uninitialized in pack
void variant3_all_uninit() {
    int x, y, z;
    consume(x, y, z);  // ERROR: x, y, z all uninitialized
}

// Variant 1.4: uninitialized double in mixed-type pack
void variant4_mixed_types_uninit() {
    int a = 1;
    double d;
    char c = 'x';
    consume(a, d, c);  // ERROR: d is uninitialized
}

// =============================================================================
// SECTION 2: Uninitialized through recursive variadic templates
// =============================================================================

// Base case
int recursive_sum() { return 0; }

template<typename T, typename... Rest>
int recursive_sum(T first, Rest... rest) {
    return first + recursive_sum(rest...);
}

// Variant 2.1: uninitialized in recursive variadic
void variant5_recursive_uninit() {
    int a = 1;
    int b;
    int c = 3;
    int result = recursive_sum(a, b, c);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 2.2: first arg uninitialized
void variant6_first_uninit() {
    int x;
    int result = recursive_sum(x, 2, 3);  // ERROR: x is uninitialized
    (void)result;
}

// Variant 2.3: last arg uninitialized
void variant7_last_uninit() {
    int z;
    int result = recursive_sum(1, 2, z);  // ERROR: z is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 3: Uninitialized through pack expansion patterns
// =============================================================================

// Variant 3.1: pack expansion in initializer list with uninit
template<typename... Args>
void expand_sinks(Args... args) {
    int dummy[] = {(sink(args), 0)...};
    (void)dummy;
}

void variant8_expand_uninit() {
    int a = 1;
    int b;
    expand_sinks(a, b);  // ERROR: b is uninitialized
}

// Variant 3.2: pack expansion in array initializer
template<typename... Args>
void expand_array(Args... args) {
    int arr[] = {args...};
    (void)arr;
}

void variant9_array_uninit() {
    int x;
    expand_array(1, x, 3);  // ERROR: x is uninitialized
}

// =============================================================================
// SECTION 4: Uninitialized through expressions in packs
// =============================================================================

// Variant 4.1: uninitialized in arithmetic, then passed via pack
void variant10_expr_uninit() {
    int x;
    consume(x + 1);  // ERROR: x is uninitialized
}

// Variant 4.2: uninitialized negation in pack
void variant11_negate_uninit() {
    int x;
    consume(-x);  // ERROR: x is uninitialized
}

// Variant 4.3: uninitialized in ternary in pack
void variant12_ternary_uninit() {
    int x;
    bool cond = true;
    consume(cond ? x : 0);  // ERROR: x is uninitialized (true branch)
}

// =============================================================================
// SECTION 5: Uninitialized through variadic lambda
// =============================================================================

// Variant 5.1: uninitialized passed to variadic lambda
void variant13_lambda_uninit() {
    auto variadic_sink = []<typename... Ts>(Ts... args) {
        (void)sizeof...(args);
    };
    int x;
    variadic_sink(x);  // ERROR: x is uninitialized
}

// Variant 5.2: uninitialized among good args to lambda
void variant14_lambda_mixed() {
    auto f = [](auto... args) {
        (void)sizeof...(args);
    };
    int a = 1;
    int b;
    f(a, b);  // ERROR: b is uninitialized
}

// =============================================================================
// SECTION 6: Uninitialized through perfect forwarding
// =============================================================================

template<typename... Args>
void forward_to_sink(Args&&... args) {
    (sink(std::forward<Args>(args)), ...);
}

// Variant 6.1: uninitialized forwarded
void variant15_forward_uninit() {
    int x;
    forward_to_sink(x);  // ERROR: x is uninitialized
}

// Variant 6.2: mix of initialized and uninitialized forwarded
void variant16_forward_mixed() {
    int a = 10;
    int b;
    forward_to_sink(a, b);  // ERROR: b is uninitialized
}

// =============================================================================
// SECTION 7: Conditional initialization with variadics
// =============================================================================

// Variant 7.1: only one branch initializes, then passed to variadic
void variant17_branch_uninit(bool cond) {
    int x;
    if (cond) {
        x = 42;
    }
    consume(x);  // ERROR: x may be uninitialized
}

// Variant 7.2: loop may not execute, then passed to variadic
void variant18_loop_uninit() {
    int x;
    for (int i = 0; i < 0; ++i) {
        x = i;
    }
    consume(x);  // ERROR: loop body never executes, x uninitialized
}
