// 712_loop_bound_gaps_xfail.cpp
// Known gaps in loop bound tracking - DTT cannot currently handle these patterns
// These require interprocedural analysis or library function semantics

#include <cstddef>
#include <cstring>

// ============================================================================
// GAP: STRLEN BOUNDS
// DTT cannot know that strlen returns a value less than the buffer size
// ============================================================================

// Variant 1: Loop copying string using strlen
void variant1_string_copy() {
    char src[] = "Hello";
    char dst[10] = {0};
    size_t len = strlen(src);  // len is 5
    for (size_t i = 0; i <= len; i++) {  // Include null terminator
        dst[i] = src[i];  // SAFE: len is 5, dst has 10 chars
        // DTT FP: doesn't know strlen(src) < sizeof(src) < sizeof(dst)
    }
}

// Variant 2: String with explicit null check
void variant2_strlen_with_size() {
    char buf[100] = "test";
    size_t len = strlen(buf);
    for (size_t i = 0; i < len; i++) {
        buf[i] = 'x';  // SAFE: strlen < buffer size
    }
}

// ============================================================================
// GAP: USER-DEFINED MIN/MAX FUNCTIONS
// DTT cannot track semantics of user-defined functions
// ============================================================================

int my_min(int a, int b) { return a < b ? a : b; }
int my_max(int a, int b) { return a > b ? a : b; }

// Variant 3: User-defined min function
void variant3_user_min() {
    int arr[100] = {0};
    int user_count = 150;  // Could be any value
    int safe_count = my_min(user_count, 100);
    for (int i = 0; i < safe_count; i++) {
        arr[i] = i;  // SAFE: safe_count <= 100
        // DTT FP: doesn't know my_min semantics
    }
}

// Variant 4: Chained min calls
void variant4_chained_min(int a, int b) {
    int arr[50] = {0};
    int bound = my_min(a, my_min(b, 50));
    for (int i = 0; i < bound; i++) {
        arr[i] = i;  // SAFE: bound <= 50
    }
}

// Variant 5: User max for lower bound
void variant5_user_max(int start) {
    int arr[100] = {0};
    int begin = my_max(start, 0);
    for (int i = begin; i < 100; i++) {
        arr[i] = i;  // SAFE: begin >= 0
    }
}

// ============================================================================
// GAP: STD::MIN/MAX (WITHOUT SPECIAL CASING)
// ============================================================================

#include <algorithm>

// Variant 6: std::min bound
void variant6_std_min(size_t n) {
    int arr[100] = {0};
    size_t bound = std::min(n, (size_t)100);
    for (size_t i = 0; i < bound; i++) {
        arr[i] = (int)i;  // SAFE: bound <= 100
        // DTT FP: doesn't know std::min semantics
    }
}

// Variant 7: std::max for lower bound
void variant7_std_max(int start) {
    int arr[100] = {0};
    int begin = std::max(start, 0);
    for (int i = begin; i < 100; i++) {
        arr[i] = i;  // SAFE: begin >= 0
    }
}

// ============================================================================
// GAP: TERNARY CONDITIONAL BOUNDS
// DTT doesn't propagate proof from ternary condition to result value
// ============================================================================

// Variant 8: Ternary min pattern
void variant8_ternary_min(size_t n) {
    int arr[100] = {0};
    size_t bound = (n > 100) ? 100 : n;  // min(n, 100)
    for (size_t i = 0; i < bound; i++) {
        arr[i] = (int)i;  // SAFE: bound <= 100
        // DTT FP: doesn't propagate n <= 100 from ternary
    }
}

// Variant 9: Ternary max pattern (lower bound)
void variant9_ternary_max(int start) {
    int arr[100] = {0};
    int begin = (start < 0) ? 0 : start;  // max(start, 0)
    for (int i = begin; i < 100; i++) {
        arr[i] = i;  // SAFE: i >= 0 and i < 100
        // DTT FP: doesn't propagate begin >= 0 from ternary
    }
}

// Variant 10: Nested ternary (clamp)
void variant10_ternary_clamp(int idx) {
    int arr[100] = {0};
    int safe_idx = (idx < 0) ? 0 : ((idx >= 100) ? 99 : idx);
    arr[safe_idx] = 42;  // SAFE: 0 <= safe_idx <= 99
    // DTT FP: doesn't propagate bounds from nested ternary
}

// ============================================================================
// GAP: COMPUTED BOUNDS FROM IF-CHECKS
// DTT doesn't track variable modifications from if-statements
// ============================================================================

// Variant 11: Bound from if-check
void variant11_bound_from_check(size_t n) {
    int arr[100] = {0};
    if (n > 100) n = 100;  // Clamp n to 100
    for (size_t i = 0; i < n; i++) {
        arr[i] = (int)i;  // SAFE: n <= 100 after check
        // DTT FP: doesn't track n modification in if
    }
}

// Variant 12: __builtin_unreachable bound
void variant12_unreachable_bound(size_t n) {
    int arr[100] = {0};
    if (n > 100) __builtin_unreachable();  // UB if n > 100
    for (size_t i = 0; i < n; i++) {
        arr[i] = (int)i;  // SAFE: n <= 100 assumed
        // DTT FP: doesn't use __builtin_unreachable as proof
    }
}

int main() {
    return 0;
}
