// 1206_fold_expression_pass.cpp
// Tests: Safe C++17 fold expression patterns — should compile without DTT errors
//
// Fold expressions expand parameter packs over binary operators:
//   (args op ...)     — unary right fold
//   (... op args)     — unary left fold
//   (args op ... op init) — binary right fold
//   (init op ... op args) — binary left fold
//
// The ellipsis (...) here is the fold operator, distinct from C varargs.

#include <cstddef>
#include <utility>

void sink(int);

// =============================================================================
// SECTION 1: Unary right folds — (args op ...)
// =============================================================================

// Variant 1.1: sum via unary right fold
template<typename... Args>
auto fold_sum(Args... args) {
    return (args + ...);  // (a1 + (a2 + (a3 + ...)))
}

void variant1_fold_sum() {
    int a = 1, b = 2, c = 3;
    auto result = fold_sum(a, b, c);  // OK: all initialized → 6
    (void)result;
}

// Variant 1.2: product via unary right fold
template<typename... Args>
auto fold_product(Args... args) {
    return (args * ...);  // (a1 * (a2 * (a3 * ...)))
}

void variant2_fold_product() {
    int x = 2, y = 3, z = 4;
    auto result = fold_product(x, y, z);  // OK: all initialized → 24
    (void)result;
}

// Variant 1.3: bitwise OR fold
template<typename... Flags>
auto fold_flags(Flags... flags) {
    return (flags | ...);
}

void variant3_fold_flags() {
    unsigned a = 0x01, b = 0x02, c = 0x04;
    auto combined = fold_flags(a, b, c);  // OK: all initialized → 0x07
    (void)combined;
}

// Variant 1.4: logical AND fold (short-circuit)
template<typename... Bools>
bool fold_all(Bools... bs) {
    return (bs && ...);  // All true?
}

void variant4_fold_all() {
    bool a = true, b = true, c = false;
    bool result = fold_all(a, b, c);  // OK: all initialized → false
    (void)result;
}

// Variant 1.5: logical OR fold
template<typename... Bools>
bool fold_any(Bools... bs) {
    return (bs || ...);  // Any true?
}

void variant5_fold_any() {
    bool a = false, b = true, c = false;
    bool result = fold_any(a, b, c);  // OK: all initialized → true
    (void)result;
}

// =============================================================================
// SECTION 2: Unary left folds — (... op args)
// =============================================================================

// Variant 2.1: left fold subtraction (order matters)
template<typename... Args>
auto fold_left_sub(Args... args) {
    return (... - args);  // (((a1 - a2) - a3) - ...)
}

void variant6_left_fold_sub() {
    int a = 100, b = 20, c = 3;
    auto result = fold_left_sub(a, b, c);  // OK: all initialized → 77
    (void)result;
}

// Variant 2.2: left fold with bitwise XOR
template<typename... Args>
auto fold_left_xor(Args... args) {
    return (... ^ args);
}

void variant7_left_fold_xor() {
    int a = 0xFF, b = 0x0F, c = 0xF0;
    auto result = fold_left_xor(a, b, c);  // OK: all initialized
    (void)result;
}

// =============================================================================
// SECTION 3: Binary folds with init value — (args op ... op init)
// =============================================================================

// Variant 3.1: binary right fold with init
template<typename... Args>
auto fold_sum_init(Args... args) {
    return (args + ... + 0);  // (a1 + (a2 + (a3 + 0)))
}

void variant8_binary_right_fold() {
    int a = 1, b = 2;
    auto result = fold_sum_init(a, b);  // OK: all initialized → 3
    (void)result;
}

// Variant 3.2: binary right fold — works with empty pack
void variant9_empty_pack_fold() {
    auto result = fold_sum_init();  // OK: empty pack → just init value 0
    (void)result;
}

// Variant 3.3: binary left fold with init
template<typename... Args>
auto fold_left_sum_init(Args... args) {
    return (0 + ... + args);  // ((((0 + a1) + a2) + a3))
}

void variant10_binary_left_fold() {
    int x = 10, y = 20, z = 30;
    auto result = fold_left_sum_init(x, y, z);  // OK: all initialized → 60
    (void)result;
}

// =============================================================================
// SECTION 4: Comma fold — call sink for each element
// =============================================================================

// Variant 4.1: comma fold to call function on each arg
template<typename... Args>
void fold_call_each(Args... args) {
    (sink(args), ...);  // Comma fold: sink(a1), sink(a2), ...
}

void variant11_comma_fold() {
    int a = 1, b = 2, c = 3;
    fold_call_each(a, b, c);  // OK: all initialized
}

// Variant 4.2: comma fold with side-effecting lambda
template<typename F, typename... Args>
void fold_apply(F f, Args... args) {
    (f(args), ...);  // Apply f to each arg
}

void variant12_apply_fold() {
    int sum = 0;
    auto accumulate = [&sum](int x) { sum += x; };
    int a = 10, b = 20;
    fold_apply(accumulate, a, b);  // OK: all initialized
    (void)sum;
}

// Variant 4.3: comma fold to construct in-place
template<typename... Args>
void fold_into_array(int* out, Args... args) {
    int i = 0;
    ((out[i++] = args), ...);  // Comma fold writing to array
}

void variant13_array_fold() {
    int arr[3];
    int a = 10, b = 20, c = 30;
    fold_into_array(arr, a, b, c);  // OK: all initialized
}

// =============================================================================
// SECTION 5: Fold expressions with comparison operators
// =============================================================================

// Variant 5.1: check if all arguments equal a value
template<typename T, typename... Args>
bool fold_all_equal(T target, Args... args) {
    return ((args == target) && ...);
}

void variant14_all_equal() {
    int target = 5;
    int a = 5, b = 5, c = 5;
    bool result = fold_all_equal(target, a, b, c);  // OK: all initialized → true
    (void)result;
}

// Variant 5.2: check if any argument exceeds threshold
template<typename T, typename... Args>
bool fold_any_exceeds(T threshold, Args... args) {
    return ((args > threshold) || ...);
}

void variant15_any_exceeds() {
    int thresh = 10;
    int a = 5, b = 15, c = 3;
    bool result = fold_any_exceeds(thresh, a, b, c);  // OK: all initialized → true
    (void)result;
}

// =============================================================================
// SECTION 6: Nested fold expressions
// =============================================================================

// Variant 6.1: fold of fold — sum of products
template<typename T, typename... Args>
auto fold_multiply_then_add(T factor, Args... args) {
    return ((args * factor) + ...);  // Nested: (a1*f + (a2*f + ...))
}

void variant16_nested_fold() {
    int factor = 2;
    int a = 1, b = 2, c = 3;
    auto result = fold_multiply_then_add(factor, a, b, c);  // OK → 12
    (void)result;
}

// Variant 6.2: fold with ternary (conditional fold)
template<typename... Args>
int fold_count_positive(Args... args) {
    return ((args > 0 ? 1 : 0) + ...);
}

void variant17_conditional_fold() {
    int a = -1, b = 5, c = 0, d = 3;
    int count = fold_count_positive(a, b, c, d);  // OK: all initialized → 2
    (void)count;
}

// =============================================================================
// SECTION 7: Fold with member access
// =============================================================================

struct Weighted {
    int value;
    int weight;
};

template<typename... Items>
int fold_weighted_sum(Items... items) {
    return ((items.value * items.weight) + ...);
}

void variant18_member_fold() {
    Weighted a{10, 2}, b{20, 3}, c{5, 1};
    int result = fold_weighted_sum(a, b, c);  // OK: all initialized → 85
    (void)result;
}

// =============================================================================
// SECTION 8: Fold with auto and concept constraints
// =============================================================================

template<typename T>
concept Numeric = requires(T a) { a + a; a * a; };

template<Numeric... Args>
auto fold_constrained_sum(Args... args) {
    return (args + ... + 0);
}

void variant19_constrained_fold() {
    int a = 1;
    double b = 2.5;
    auto result = fold_constrained_sum(a, b);  // OK: all Numeric, all initialized
    (void)result;
}

// =============================================================================
// SECTION 9: Fold in lambda context
// =============================================================================

void variant20_lambda_fold() {
    auto sum_all = [](auto... args) {
        return (args + ... + 0);
    };

    int a = 10, b = 20;
    auto result = sum_all(a, b);  // OK: all initialized
    (void)result;
}

void variant21_lambda_comma_fold() {
    int total = 0;
    auto accumulate_all = [&total](auto... args) {
        ((total += args), ...);  // Comma fold in lambda
    };

    int a = 1, b = 2, c = 3;
    accumulate_all(a, b, c);  // OK: all initialized → total = 6
    (void)total;
}

// =============================================================================
// SECTION 10: Fold with type traits
// =============================================================================

template<typename... Ts>
constexpr bool all_integral() {
    return (std::is_integral_v<Ts> && ...);  // Fold over type trait
}

template<typename... Ts>
constexpr bool any_pointer() {
    return (std::is_pointer_v<Ts> || ...);  // Fold over type trait
}

void variant22_type_trait_fold() {
    constexpr bool all_int = all_integral<int, short, long>();  // true
    constexpr bool has_ptr = any_pointer<int, double, char*>(); // true
    (void)all_int;
    (void)has_ptr;
}

// =============================================================================
// SECTION 11: Fold with assignment operators (+=, |=, &=)
// =============================================================================

// Variant 11.1: fold with += accumulation
template<typename... Args>
int fold_add_assign(Args... args) {
    int sum = 0;
    ((sum += args), ...);  // Comma fold with +=
    return sum;
}

void variant23_fold_add_assign() {
    int a = 10, b = 20, c = 30;
    int result = fold_add_assign(a, b, c);  // OK: all initialized → 60
    (void)result;
}

// Variant 11.2: fold with |= for flag accumulation
template<typename... Flags>
unsigned fold_set_flags(Flags... flags) {
    unsigned result = 0;
    ((result |= flags), ...);
    return result;
}

void variant24_fold_or_assign() {
    unsigned a = 0x01, b = 0x04, c = 0x10;
    unsigned flags = fold_set_flags(a, b, c);  // OK: all initialized → 0x15
    (void)flags;
}

// Variant 11.3: fold with &= for mask intersection
template<typename... Masks>
unsigned fold_and_masks(unsigned init, Masks... masks) {
    ((init &= masks), ...);
    return init;
}

void variant25_fold_and_assign() {
    unsigned a = 0xFF, b = 0x0F;
    unsigned result = fold_and_masks(0xFF, a, b);  // OK: all initialized → 0x0F
    (void)result;
}

// Variant 11.4: fold with *= accumulation
template<typename... Args>
auto fold_mul_assign(Args... args) {
    int product = 1;
    ((product *= args), ...);
    return product;
}

void variant26_fold_mul_assign() {
    int a = 2, b = 3, c = 4;
    auto result = fold_mul_assign(a, b, c);  // OK: all initialized → 24
    (void)result;
}

// =============================================================================
// SECTION 12: Fold with <<= / >>= (shift-assign)
// =============================================================================

// Variant 12.1: fold with <<= for bit shifting
template<typename... Shifts>
unsigned fold_shift_left(unsigned val, Shifts... shifts) {
    ((val <<= shifts), ...);
    return val;
}

void variant27_fold_shift() {
    unsigned s1 = 1, s2 = 2, s3 = 3;
    unsigned result = fold_shift_left(1u, s1, s2, s3);  // OK → 1 << 6 = 64
    (void)result;
}
