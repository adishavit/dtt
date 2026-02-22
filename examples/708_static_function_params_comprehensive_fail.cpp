// 708_static_function_params_pass.cpp
// Tests for static/internal function parameter analysis
// DTT should analyze call sites for static functions and NOT warn
// when all callers pass non-NULL values

#include <cstddef>
#include <cstdlib>
#include <cstring>

// ============================================================================
// BASIC STATIC FUNCTION PATTERNS
// ============================================================================

// Variant 1: Simple static helper with single caller
static int get_length(const char* s) {
    return (int)strlen(s);  // DTT warns, but all callers pass non-NULL
}

void variant1_single_caller() {
    const char* str = "Hello";  // Known non-NULL
    int len = get_length(str);  // SAFE: str is definitely not NULL
    (void)len;
}

// Variant 2: Static helper with multiple safe callers
static int sum_array(const int* arr, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        total += arr[i];  // DTT warns about arr, but callers guarantee non-NULL
    }
    return total;
}

void variant2_caller_a() {
    int data[] = {1, 2, 3, 4, 5};
    int s = sum_array(data, 5);  // SAFE: stack array, never NULL
    (void)s;
}

void variant2_caller_b() {
    int* heap_data = (int*)malloc(10 * sizeof(int));
    if (!heap_data) return;  // Guard ensures non-NULL
    int s = sum_array(heap_data, 10);  // SAFE: guarded by null check
    (void)s;
    free(heap_data);
}

// Variant 3: Static helper called in loop
static void process_byte(unsigned char* ptr) {
    *ptr = (*ptr) ^ 0xFF;  // DTT warns, but caller guarantees valid ptr
}

void variant3_loop_caller() {
    unsigned char buffer[100];
    memset(buffer, 0, sizeof(buffer));
    for (int i = 0; i < 100; i++) {
        process_byte(&buffer[i]);  // SAFE: &buffer[i] is never NULL
    }
}

// Variant 4: Chained static helpers
static int char_to_digit(const char* c) {
    return *c - '0';  // Dereferences c
}

static int parse_digit(const char* s) {
    return char_to_digit(s);  // Passes s directly
}

void variant4_chain_caller() {
    const char* num = "5";
    int d = parse_digit(num);  // SAFE: "5" is never NULL
    (void)d;
}

// Variant 5: Static helper with output parameter
static void swap_values(int* a, int* b) {
    int tmp = *a;  // DTT warns about a
    *a = *b;       // DTT warns about b
    *b = tmp;
}

void variant5_output_param_caller() {
    int x = 10, y = 20;
    swap_values(&x, &y);  // SAFE: &x and &y are never NULL
}

// ============================================================================
// NAMESPACE/CLASS STATIC PATTERNS
// ============================================================================

namespace internal {

// Variant 6: Static in namespace
static void trim_string(char* s) {
    // Trim trailing whitespace
    char* end = s + strlen(s) - 1;
    while (end > s && *end == ' ') {
        *end = '\0';
        end--;
    }
}

}  // namespace internal

void variant6_namespace_caller() {
    char buf[] = "Hello   ";
    internal::trim_string(buf);  // SAFE: buf is stack array
}

// Variant 7: Private static method pattern (simulated)
class Parser {
private:
    static int count_chars(const char* s, char c) {
        int count = 0;
        while (*s) {  // Dereferences s
            if (*s == c) count++;
            s++;
        }
        return count;
    }

public:
    int countSpaces(const char* input) {
        if (!input) return 0;  // Public method guards
        return count_chars(input, ' ');  // SAFE: guarded above
    }
};

void variant7_class_caller() {
    Parser p;
    int spaces = p.countSpaces("Hello World");
    (void)spaces;
}

// ============================================================================
// ANONYMOUS NAMESPACE PATTERNS
// ============================================================================

namespace {

// Variant 8: Function in anonymous namespace (effectively static)
void copy_bytes(const char* src, char* dst, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];  // DTT warns about both
    }
}

}  // anonymous namespace

void variant8_anon_namespace_caller() {
    const char* source = "Test";
    char dest[10];
    copy_bytes(source, dest, 5);  // SAFE: both are valid
}

// ============================================================================
// GUARD PATTERNS BEFORE CALL
// ============================================================================

// Variant 9: Caller guards with if-check
static void use_string(const char* s) {
    char c = s[0];  // Dereferences s
    (void)c;
}

void variant9_if_guard_caller(const char* input) {
    if (input != nullptr) {
        use_string(input);  // SAFE: guarded by if
    }
}

// Variant 10: Caller guards with early return
static int string_first_char(const char* s) {
    return (int)s[0];
}

int variant10_early_return_caller(const char* input) {
    if (!input) return -1;  // Early return if NULL
    return string_first_char(input);  // SAFE: past the guard
}

// Variant 11: Caller guards with assert (debug builds)
static void critical_operation(int* data) {
    *data = *data + 1;
}

void variant11_assert_caller(int* ptr) {
    // In production, assert may be compiled out, but the contract exists
    // assert(ptr != nullptr);
    if (!ptr) __builtin_unreachable();  // Tells compiler ptr is non-NULL
    critical_operation(ptr);  // SAFE: unreachable guards
}

// ============================================================================
// LITERAL AND CONSTANT CALLERS
// ============================================================================

// Variant 12: Called with string literal
static size_t measure(const char* s) {
    return strlen(s);
}

void variant12_literal_caller() {
    size_t len = measure("Hello World");  // SAFE: literal is never NULL
    (void)len;
}

// Variant 13: Called with address-of
static void increment(int* p) {
    (*p)++;
}

void variant13_address_of_caller() {
    int value = 0;
    increment(&value);  // SAFE: &value is never NULL
}

// Variant 14: Called with array decay
static int first_element(const int* arr) {
    return arr[0];
}

void variant14_array_decay_caller() {
    int data[] = {10, 20, 30};
    int f = first_element(data);  // SAFE: array decay is never NULL
    (void)f;
}

// ============================================================================
// MULTIPLE PARAMETER PATTERNS
// ============================================================================

// Variant 15: Multiple pointer params, all safe
static void copy_struct(const void* src, void* dst, size_t size) {
    memcpy(dst, src, size);  // Both src and dst dereferenced
}

void variant15_multi_param_caller() {
    int a = 42;
    int b = 0;
    copy_struct(&a, &b, sizeof(int));  // SAFE: both are addresses
}

// Variant 16: Mixed NULL and non-NULL params
static void optional_copy(const char* src, char* dst, size_t n) {
    if (src && dst) {
        strncpy(dst, src, n);
    }
}

void variant16_mixed_caller() {
    char buf[100];
    optional_copy("Hello", buf, sizeof(buf));  // SAFE: function handles NULL
    optional_copy(nullptr, buf, sizeof(buf));  // SAFE: function handles NULL
}

// ============================================================================
// RETURN VALUE USED AS ARGUMENT
// ============================================================================

// Variant 17: Return value from non-NULL function
static const char* get_prefix() {
    return "PREFIX_";  // Always returns non-NULL
}

static void use_prefix(const char* p) {
    char c = p[0];
    (void)c;
}

void variant17_return_value_caller() {
    use_prefix(get_prefix());  // SAFE: get_prefix never returns NULL
}

// Variant 18: Guarded malloc result
static void fill_buffer(char* buf, int size) {
    for (int i = 0; i < size; i++) {
        buf[i] = 'x';
    }
}

void variant18_guarded_malloc_caller() {
    char* buf = (char*)malloc(100);
    if (buf) {
        fill_buffer(buf, 100);  // SAFE: guarded by if
        free(buf);
    }
}

// ============================================================================
// CONTRAST: PUBLIC FUNCTIONS (SHOULD STILL WARN)
// ============================================================================

// Variant 19: Non-static function - SHOULD warn (external callers unknown)
void public_function(const char* s) {
    // DTT SHOULD warn here - callers outside this file are unknown
    char c = s[0];
    (void)c;
}

// Variant 20: Header-declared function - SHOULD warn
// (imagine this is declared in a header)
extern void external_function(int* p);

void external_function(int* p) {
    // DTT SHOULD warn - external callers could pass NULL
    *p = 42;
}

// ============================================================================
// EDGE CASES
// ============================================================================

// Variant 21: Recursive static function
static int recursive_length(const char* s) {
    if (*s == '\0') return 0;
    return 1 + recursive_length(s + 1);  // Recursive call is also safe
}

void variant21_recursive_caller() {
    int len = recursive_length("Hello");  // SAFE: initial call with literal
    (void)len;
}

// Variant 22: Static function pointer (harder to analyze)
static void operation_a(int* p) { *p += 1; }
static void operation_b(int* p) { *p *= 2; }

void variant22_function_pointer() {
    void (*op)(int*) = operation_a;
    int value = 5;
    op(&value);  // SAFE: &value is non-NULL
    op = operation_b;
    op(&value);  // SAFE: still &value
}

// Variant 23: Static function called via conditional
static void safe_increment(int* p) {
    (*p)++;
}

void variant23_conditional_call(bool flag) {
    int a = 0, b = 0;
    if (flag) {
        safe_increment(&a);  // SAFE
    } else {
        safe_increment(&b);  // SAFE
    }
}

// Variant 24: Template-like pattern with static
template<typename T>
static T* get_first(T* arr, size_t n) {
    if (n == 0) return nullptr;
    return &arr[0];  // arr dereference
}

void variant24_template_caller() {
    int data[] = {1, 2, 3};
    int* first = get_first(data, 3);  // SAFE: data is stack array
    (void)first;
}

// Variant 25: Static inline function
static inline int peek_byte(const unsigned char* buf) {
    return *buf;
}

void variant25_inline_caller() {
    unsigned char data[] = {0x42, 0x43};
    int b = peek_byte(data);  // SAFE: array decay
    (void)b;
}

// ============================================================================
// MIXED CALLERS - WARN AT SPECIFIC CALL SITE
// These demonstrate cases where DTT should warn at the OFFENDING call site,
// not at the function definition
// ============================================================================

// Variant 26: One bad caller among many good ones
static void require_nonnull(const char* s) {
    char c = s[0];  // DON'T warn here - analyze callers instead
    (void)c;
}

void variant26_good_caller_a() {
    require_nonnull("literal");  // SAFE - don't warn
}

void variant26_good_caller_b() {
    char buf[] = "stack";
    require_nonnull(buf);  // SAFE - don't warn
}

void variant26_bad_caller(const char* maybe_null) {
    // This caller SHOULD trigger a warning - maybe_null is unknown
    require_nonnull(maybe_null);  // WARN HERE: 'maybe_null' may be NULL
}

// Variant 27: Conditional NULL passed
static void process_data(int* data, int count) {
    for (int i = 0; i < count; i++) {
        data[i] *= 2;  // DON'T warn here
    }
}

void variant27_sometimes_null(bool have_data) {
    int* ptr = nullptr;
    if (have_data) {
        ptr = (int*)malloc(10 * sizeof(int));
    }
    // ptr may be NULL even if have_data was true (malloc fail)
    // or definitely NULL if have_data was false
    process_data(ptr, 10);  // WARN HERE: 'ptr' may be NULL
    free(ptr);
}

void variant27_always_valid() {
    int arr[10] = {0};
    process_data(arr, 10);  // SAFE - don't warn
}

// Variant 28: NULL from function return
static void use_buffer(char* buf) {
    buf[0] = 'X';  // DON'T warn here
}

char* might_return_null() {
    if (rand() % 2) return nullptr;
    return (char*)malloc(100);
}

void variant28_unchecked_return() {
    char* buf = might_return_null();
    use_buffer(buf);  // WARN HERE: return value not checked
    free(buf);
}

void variant28_checked_return() {
    char* buf = might_return_null();
    if (buf) {
        use_buffer(buf);  // SAFE - guarded
        free(buf);
    }
}

// Variant 29: One caller explicitly passes NULL
static void optional_cleanup(void* resource) {
    if (resource) {
        free(resource);
    }
}

void variant29_explicit_null() {
    optional_cleanup(nullptr);  // This is intentional - function handles it
    // But if function DIDN'T handle NULL:
}

static void non_optional_cleanup(void* resource) {
    // This function does NOT handle NULL
    memset(resource, 0, 10);  // DON'T warn here
}

void variant29_bad_explicit_null() {
    non_optional_cleanup(nullptr);  // WARN HERE: passing NULL to function that dereferences
}

void variant29_good_caller() {
    void* p = malloc(100);
    if (p) {
        non_optional_cleanup(p);  // SAFE - guarded
        free(p);
    }
}

// Variant 30: Propagated unknown nullability
static void inner_helper(const char* s) {
    char c = *s;
    (void)c;
}

static void outer_helper(const char* s) {
    inner_helper(s);  // Propagates nullability
}

void variant30_good_outer_caller() {
    outer_helper("safe");  // SAFE - literal propagates through
}

void variant30_bad_outer_caller(const char* unknown) {
    outer_helper(unknown);  // WARN HERE (or at inner_helper call): unknown nullability propagates
}

int main() {
    // These tests verify that DTT can analyze call sites
    // for static functions and suppress false positives
    // while warning at specific offending call sites
    variant1_single_caller();
    variant2_caller_a();
    variant2_caller_b();
    return 0;
}
