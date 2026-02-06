// 1207_fold_expression_fail.cpp
// Tests: Unsafe fold expression patterns — DTT should catch these
// Uninitialized values used in fold expressions over parameter packs

void sink(int);

// =============================================================================
// SECTION 1: Uninitialized in unary right folds
// =============================================================================

template<typename... Args>
auto fold_sum(Args... args) {
    return (args + ...);
}

// Variant 1.1: uninitialized in sum fold
void variant1_fold_uninit() {
    int a = 1;
    int b;
    int c = 3;
    auto result = fold_sum(a, b, c);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 1.2: all uninitialized in fold
void variant2_all_uninit_fold() {
    int x, y, z;
    auto result = fold_sum(x, y, z);  // ERROR: x, y, z all uninitialized
    (void)result;
}

// Variant 1.3: single uninitialized in fold (degenerates to identity)
void variant3_single_uninit() {
    int x;
    auto result = fold_sum(x);  // ERROR: x is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 2: Uninitialized in logical folds
// =============================================================================

template<typename... Bools>
bool fold_all(Bools... bs) {
    return (bs && ...);
}

template<typename... Bools>
bool fold_any(Bools... bs) {
    return (bs || ...);
}

// Variant 2.1: uninitialized bool in AND fold
void variant4_and_fold_uninit() {
    bool a = true;
    bool b;
    bool c = true;
    bool result = fold_all(a, b, c);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 2.2: uninitialized bool in OR fold
void variant5_or_fold_uninit() {
    bool a = false;
    bool b;
    bool result = fold_any(a, b);  // ERROR: b is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 3: Uninitialized in binary folds with init
// =============================================================================

template<typename... Args>
auto fold_sum_init(Args... args) {
    return (args + ... + 0);
}

// Variant 3.1: uninitialized in binary fold
void variant6_binary_fold_uninit() {
    int a = 1;
    int b;
    auto result = fold_sum_init(a, b);  // ERROR: b is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 4: Uninitialized in comma folds (side effects)
// =============================================================================

template<typename... Args>
void fold_sink_each(Args... args) {
    (sink(args), ...);
}

// Variant 4.1: uninitialized through comma fold
void variant7_comma_fold_uninit() {
    int a = 1;
    int b;
    fold_sink_each(a, b);  // ERROR: b is uninitialized
}

// Variant 4.2: single uninitialized in comma fold
void variant8_comma_single_uninit() {
    int x;
    fold_sink_each(x);  // ERROR: x is uninitialized
}

// =============================================================================
// SECTION 5: Uninitialized in fold with expressions
// =============================================================================

template<typename T, typename... Args>
auto fold_multiply_add(T factor, Args... args) {
    return ((args * factor) + ...);
}

// Variant 5.1: uninitialized multiplied in fold
void variant9_expr_fold_uninit() {
    int factor = 2;
    int a = 1;
    int b;
    auto result = fold_multiply_add(factor, a, b);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 5.2: uninitialized factor
void variant10_factor_uninit() {
    int factor;
    int a = 1, b = 2;
    auto result = fold_multiply_add(factor, a, b);  // ERROR: factor is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 6: Uninitialized in comparison folds
// =============================================================================

template<typename T, typename... Args>
bool fold_all_equal(T target, Args... args) {
    return ((args == target) && ...);
}

// Variant 6.1: uninitialized in comparison fold
void variant11_cmp_fold_uninit() {
    int target = 5;
    int a = 5;
    int b;
    bool result = fold_all_equal(target, a, b);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 6.2: uninitialized target
void variant12_target_uninit() {
    int target;
    int a = 5, b = 5;
    bool result = fold_all_equal(target, a, b);  // ERROR: target is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 7: Uninitialized in lambda fold
// =============================================================================

// Variant 7.1: uninitialized passed to lambda with fold
void variant13_lambda_fold_uninit() {
    auto sum = [](auto... args) { return (args + ... + 0); };
    int a = 1;
    int b;
    auto result = sum(a, b);  // ERROR: b is uninitialized
    (void)result;
}

// Variant 7.2: uninitialized in comma fold lambda
void variant14_lambda_comma_uninit() {
    int total = 0;
    auto acc = [&total](auto... args) { ((total += args), ...); };
    int a = 1;
    int b;
    acc(a, b);  // ERROR: b is uninitialized
}

// =============================================================================
// SECTION 8: Uninitialized with conditional fold
// =============================================================================

template<typename... Args>
int fold_count_positive(Args... args) {
    return ((args > 0 ? 1 : 0) + ...);
}

// Variant 8.1: uninitialized in conditional fold
void variant15_cond_fold_uninit() {
    int a = -1;
    int b;
    int c = 3;
    int count = fold_count_positive(a, b, c);  // ERROR: b is uninitialized
    (void)count;
}

// =============================================================================
// SECTION 9: Uninitialized in left folds
// =============================================================================

template<typename... Args>
auto fold_left_sub(Args... args) {
    return (... - args);
}

// Variant 9.1: uninitialized in left fold
void variant16_left_fold_uninit() {
    int a = 100;
    int b;
    int c = 3;
    auto result = fold_left_sub(a, b, c);  // ERROR: b is uninitialized
    (void)result;
}

// =============================================================================
// SECTION 10: Conditionally uninitialized through fold
// =============================================================================

template<typename... Args>
void fold_consume(Args... args) {
    (sink(args), ...);
}

// Variant 10.1: maybe-uninitialized passed to fold
void variant17_maybe_uninit(bool cond) {
    int x;
    if (cond) {
        x = 42;
    }
    fold_consume(x);  // ERROR: x may be uninitialized
}

// Variant 10.2: compound assignment then fold
void variant18_compound_fold() {
    int x;
    x += 1;  // ERROR: reads x before initialization
    fold_consume(x);
}
