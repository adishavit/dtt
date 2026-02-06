// 1204_variadic_template_pass.cpp
// Tests: Safe C++ variadic template patterns — parameter packs, expansion, recursion
// DTT should allow these — all values are properly initialized and tracked
//
// Variadic templates use template<typename... Ts> and function parameter packs.
// The ellipsis (...) appears in template parameter lists, function parameters,
// and pack expansion contexts.

#include <cstddef>
#include <utility>

// =============================================================================
// SECTION 1: Basic variadic function templates with initialized args
// =============================================================================

// Variant 1.1: simple variadic function — all args initialized
template<typename... Args>
void consume(Args... args) {
    // Pack expansion into an initializer list (comma trick)
    (void)sizeof...(args);
}

void variant1_basic_call() {
    int a = 1;
    double b = 2.0;
    char c = '3';
    consume(a, b, c);  // OK: all initialized
}

// Variant 1.2: variadic with literals (always safe)
void variant2_literals() {
    consume(1, 2.0, '3', "four");  // OK: literals always initialized
}

// Variant 1.3: variadic with zero args (empty pack)
void variant3_empty_pack() {
    consume();  // OK: empty parameter pack is valid
}

// =============================================================================
// SECTION 2: Recursive variadic templates (classic pre-C++17 pattern)
// =============================================================================

// Base case
int recursive_sum() { return 0; }

// Recursive case: peel one arg, recurse on rest
template<typename T, typename... Rest>
int recursive_sum(T first, Rest... rest) {
    return first + recursive_sum(rest...);  // Pack expansion in call
}

// Variant 2.1: recursive sum with initialized values
void variant4_recursive_sum() {
    int a = 1, b = 2, c = 3, d = 4;
    int result = recursive_sum(a, b, c, d);  // OK: all initialized
    (void)result;
}

// Variant 2.2: recursive with single arg (base case hit quickly)
void variant5_single_arg() {
    int x = 42;
    int result = recursive_sum(x);  // OK: x initialized
    (void)result;
}

// =============================================================================
// SECTION 3: sizeof... operator on parameter packs
// =============================================================================

template<typename... Ts>
constexpr size_t pack_size() {
    return sizeof...(Ts);  // OK: sizeof... never evaluates arguments
}

template<typename... Args>
size_t arg_count(Args... args) {
    return sizeof...(args);  // OK: sizeof... is compile-time
}

// Variant 3.1: sizeof... on type pack
void variant6_sizeof_type_pack() {
    constexpr size_t n = pack_size<int, double, char>();  // OK: 3
    (void)n;
}

// Variant 3.2: sizeof... on function parameter pack
void variant7_sizeof_arg_pack() {
    int a = 1, b = 2;
    size_t n = arg_count(a, b);  // OK: sizeof... doesn't evaluate args
    (void)n;
}

// =============================================================================
// SECTION 4: Pack expansion in different contexts
// =============================================================================

void sink(int);
void multi_sink(int, double, char);

// Variant 4.1: pack expansion in function call arguments
template<typename... Args>
void expand_in_call(Args... args) {
    // Each arg expanded as a separate argument
    multi_sink(args...);
}

void variant8_call_expansion() {
    int a = 1;
    double b = 2.0;
    char c = '3';
    expand_in_call(a, b, c);  // OK: all initialized
}

// Variant 4.2: pack expansion in initializer list
template<typename... Args>
void expand_in_init_list(Args... args) {
    int dummy[] = {(sink(args), 0)...};  // Pack expansion with comma operator
    (void)dummy;
}

void variant9_init_list_expansion() {
    int a = 10, b = 20, c = 30;
    expand_in_init_list(a, b, c);  // OK: all initialized
}

// Variant 4.3: pack expansion in array
template<typename... Args>
void expand_in_array(Args... args) {
    int arr[] = {args...};  // Pack expansion into array initializer
    (void)arr;
}

void variant10_array_expansion() {
    expand_in_array(1, 2, 3, 4, 5);  // OK: literals
}

// =============================================================================
// SECTION 5: Variadic class templates
// =============================================================================

// Variant 5.1: variadic tuple-like struct
template<typename... Ts>
struct TypeList {
    static constexpr size_t size = sizeof...(Ts);
};

void variant11_type_list() {
    TypeList<int, double, char> tl;
    (void)tl;
    constexpr size_t n = TypeList<int, double>::size;  // OK: compile-time
    (void)n;
}

// Variant 5.2: variadic inheritance (mixin pattern)
template<typename... Bases>
struct Mixin : Bases... {};

struct HasX { int x = 0; };
struct HasY { int y = 0; };

void variant12_mixin() {
    Mixin<HasX, HasY> m;
    m.x = 10;  // OK: from HasX base
    m.y = 20;  // OK: from HasY base
    (void)m;
}

// =============================================================================
// SECTION 6: Nested pack expansion
// =============================================================================

template<typename T>
T identity(T val) { return val; }

// Variant 6.1: nested pack expansion — apply function to each arg
template<typename... Args>
void apply_identity(Args... args) {
    int dummy[] = {(sink(identity(args)), 0)...};
    (void)dummy;
}

void variant13_nested_expansion() {
    int a = 1, b = 2, c = 3;
    apply_identity(a, b, c);  // OK: identity on initialized values
}

// Variant 6.2: pack expansion with index_sequence
template<typename T, size_t... Is>
void access_by_indices(T* arr, std::index_sequence<Is...>) {
    int dummy[] = {(sink(arr[Is]), 0)...};  // OK: expand indices
    (void)dummy;
}

void variant14_index_expansion() {
    int arr[] = {10, 20, 30};
    access_by_indices(arr, std::index_sequence<0, 1, 2>{});  // OK
}

// =============================================================================
// SECTION 7: Variadic templates with auto and decltype
// =============================================================================

// Variant 7.1: auto return type deduced from pack
template<typename T, typename... Rest>
auto first_of(T val, Rest...) {
    return val;
}

void variant15_auto_return() {
    int x = 42;
    auto result = first_of(x, 2.0, '3');  // OK: x initialized
    (void)result;
}

// Variant 7.2: decltype on parameter pack element
template<typename T, typename... Rest>
decltype(auto) decltype_first(T&& val, Rest&&...) {
    return static_cast<T&&>(val);
}

void variant16_decltype_pack() {
    int x = 10;
    auto& ref = decltype_first(x, 20, 30);  // OK: x initialized
    (void)ref;
}

// =============================================================================
// SECTION 8: Variadic template with constraints (C++20 concepts)
// =============================================================================

template<typename T>
concept Arithmetic = requires(T a, T b) { a + b; };

template<Arithmetic... Args>
auto constrained_sum(Args... args) {
    return (args + ...);  // Fold expression (covered more in 1206)
}

void variant17_constrained_variadic() {
    int a = 1, b = 2, c = 3;
    auto result = constrained_sum(a, b, c);  // OK: all Arithmetic, all initialized
    (void)result;
}

// =============================================================================
// SECTION 9: Variadic lambda (C++20 template lambda)
// =============================================================================

void variant18_variadic_lambda() {
    auto print_all = []<typename... Ts>(Ts... args) {
        (void)sizeof...(args);
    };

    int x = 1;
    double y = 2.0;
    print_all(x, y);  // OK: all initialized
}

// Variant 9.2: variadic lambda with captures
void variant19_variadic_lambda_capture() {
    int base = 100;
    auto add_base = [base]<typename... Ts>(Ts... args) {
        return (base + ... + args);
    };

    int a = 1, b = 2;
    auto result = add_base(a, b);  // OK: base, a, b all initialized
    (void)result;
}

// =============================================================================
// SECTION 10: Perfect forwarding patterns
// =============================================================================

struct Widget {
    int value;
    Widget(int v) : value(v) {}
};

template<typename T, typename... Args>
T make(Args&&... args) {
    return T(std::forward<Args>(args)...);  // Perfect forwarding
}

void variant20_perfect_forward() {
    int v = 42;
    Widget w = make<Widget>(v);  // OK: v initialized, forwarded
    (void)w;
}

// Variant 10.2: forwarding to placement-style construction
template<typename... Args>
void emplace_like(Args&&... args) {
    auto val = Widget(std::forward<Args>(args)...);
    (void)val;
}

void variant21_emplace_pattern() {
    int x = 99;
    emplace_like(x);  // OK: x initialized
}
