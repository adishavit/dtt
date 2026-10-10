// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 843_scanf_fail.cpp
// Tests: Unsafe scanf usage that DTT SHOULD catch

#include <cstdio>
#include <cstring>

// =============================================================================
// SECTION 1: Use variable without checking return
// =============================================================================

// Variant 1.1: use scanf result without checking return
void test_scanf_no_check() {
    int x;
    scanf("%d", &x);
    int y = x + 1;  // ERROR: x might be uninitialized  // dtt-error {{uninitialized variable}}
    (void)y;
}

// Variant 1.2: use multiple variables without checking
void test_scanf_multiple_no_check() {
    int a, b;
    scanf("%d %d", &a, &b);
    int sum = a + b;  // ERROR: a and/or b might be uninitialized  // dtt-error {{uninitialized variable 'a'}} // dtt-error {{uninitialized variable 'b'}}
    (void)sum;
}

// Variant 1.3: sscanf without check
void test_sscanf_no_check() {
    const char* str = "not a number";
    int x;
    sscanf(str, "%d", &x);
    int y = x * 2;  // ERROR: x might be uninitialized  // dtt-error {{uninitialized variable}}
    (void)y;
}

// =============================================================================
// SECTION 2: Use variable in wrong branch
// =============================================================================

// Variant 2.1: use in failure case
void test_wrong_branch() {
    int x;
    if (scanf("%d", &x) == 1) {
        // OK here
    } else {
        int y = x;  // KNOWN GAP (not asserted): x is uninitialized in else branch
        (void)y;
    }
}

// Variant 2.2: use when scanf returns 0
void test_scanf_zero_return() {
    int x;
    if (scanf("%d", &x) == 0) {
        int y = x;  // KNOWN GAP (not asserted): x is uninitialized (format mismatch)
        (void)y;
    }
}

// =============================================================================
// SECTION 3: Buffer overflow with unbounded %s
// =============================================================================

// Variant 3.1: %s without bound
void test_unbounded_string() {
    char buf[10];
    scanf("%s", buf);  // KNOWN GAP (not asserted): can overflow buf
    (void)buf;
}

// Variant 3.2: sscanf with unbounded %s
void test_sscanf_unbounded() {
    char buf[10];
    const char* input = "verylongstringthatoverflows";
    sscanf(input, "%s", buf);  // KNOWN GAP (not asserted): can overflow buf
    (void)buf;
}

// =============================================================================
// SECTION 4: Use after partial read
// =============================================================================

// Variant 4.1: assume all variables read on partial success
void test_partial_read_error() {
    int a, b, c;
    if (scanf("%d %d %d", &a, &b, &c) >= 1) {
        int sum = a + b + c;  // ERROR: b and c might be uninitialized  // dtt-error {{uninitialized variable 'b'}} // dtt-error {{uninitialized variable 'c'}}
        (void)sum;
    }
}

// Variant 4.2: wrong count check
void test_wrong_count() {
    int a, b;
    if (scanf("%d %d", &a, &b) == 1) {  // Only 1 succeeded
        int product = a * b;  // ERROR: b is uninitialized  // dtt-error {{uninitialized variable 'b'}}
        (void)product;
    }
}

// =============================================================================
// SECTION 5: fscanf without file check
// =============================================================================

// Variant 5.1: fscanf on potentially null FILE*
void test_fscanf_no_file_check() {
    FILE* fp = fopen("test.txt", "r");
    int x;
    fscanf(fp, "%d", &x);  // ERROR: fp might be null  // dtt-error {{_nonnull}}
    if (fp) fclose(fp);
}

// =============================================================================
// SECTION 6: Stale return value check
// =============================================================================

// Variant 6.1: check return but use different variable
void test_stale_check() {
    int x, y;
    if (scanf("%d", &x) == 1) {
        // x is OK, but...
    }
    scanf("%d", &y);
    int z = y + 1;  // ERROR: second scanf not checked  // dtt-error {{uninitialized variable}}
    (void)z;
}

// =============================================================================
// SECTION 7: Wrong format specifier
// =============================================================================

// Variant 7.1: format mismatch
void test_format_mismatch() {
    int x;
    // Input "hello" with %d format
    scanf("%d", &x);  // Returns 0, x uninitialized
    int y = x;  // KNOWN GAP (not asserted): x likely uninitialized
    (void)y;
}

// =============================================================================
// SECTION 8: EOF not handled
// =============================================================================

// Variant 8.1: assume success when might be EOF
void test_eof_not_handled() {
    int x;
    int ret = scanf("%d", &x);
    // ret could be EOF (-1) or 0 or 1
    if (ret != 0) {  // Wrong check - includes EOF
        int y = x;  // KNOWN GAP (not asserted): x might be uninitialized on EOF
        (void)y;
    }
}

// =============================================================================
// SECTION 9: Use in loop without per-iteration check
// =============================================================================

// Variant 9.1: loop continues after scanf failure
void test_loop_no_check() {
    int arr[10];
    for (int i = 0; i < 10; ++i) {
        scanf("%d", &arr[i]);  // Might fail mid-loop
    }
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        sum += arr[i];  // KNOWN GAP (not asserted): some elements might be uninitialized
    }
    (void)sum;
}

// =============================================================================
// SECTION 10: Null pointer to scanf
// =============================================================================

// Variant 10.1: null string to sscanf
void test_null_string() {
    const char* str = nullptr;
    int x;
    sscanf(str, "%d", &x);  // KNOWN GAP (not asserted): null string
}

// Variant 10.2: null destination
void test_null_dest() {
    int* p = nullptr;
    scanf("%d", p);  // KNOWN GAP (not asserted): null destination pointer
}

int main() {
    return 0;
}
