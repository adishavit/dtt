// 842_scanf_pass.cpp
// Tests: Safe scanf usage that should compile without DTT errors

#include <cstdio>
#include <cstring>

// =============================================================================
// SECTION 1: Check return value before using variables
// =============================================================================

// Variant 1.1: if (scanf == 1) then use variable
void test_scanf_check_return() {
    int x;
    if (scanf("%d", &x) == 1) {
        // OK: scanf succeeded, x is initialized
        (void)x;
    }
}

// Variant 1.2: check return >= expected
void test_scanf_check_multiple() {
    int a, b;
    if (scanf("%d %d", &a, &b) == 2) {
        // OK: both variables initialized
        (void)(a + b);
    }
}

// Variant 1.3: early return on scanf failure
void test_scanf_early_return() {
    int x;
    if (scanf("%d", &x) != 1) return;
    // OK: x is initialized
    (void)x;
}

// =============================================================================
// SECTION 2: sscanf with return check
// =============================================================================

// Variant 2.1: sscanf with check
void test_sscanf_check() {
    const char* str = "42";
    int x;
    if (sscanf(str, "%d", &x) == 1) {
        // OK: x is initialized
        (void)x;
    }
}

// Variant 2.2: sscanf multiple values
void test_sscanf_multiple() {
    const char* str = "10 20 30";
    int a, b, c;
    if (sscanf(str, "%d %d %d", &a, &b, &c) == 3) {
        // OK: all initialized
        (void)(a + b + c);
    }
}

// =============================================================================
// SECTION 3: fscanf with file and return check
// =============================================================================

// Variant 3.1: fscanf with both checks
void test_fscanf_check() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        int x;
        if (fscanf(fp, "%d", &x) == 1) {
            // OK: file open and read succeeded
            (void)x;
        }
        fclose(fp);
    }
}

// =============================================================================
// SECTION 4: Use bounded format specifiers
// =============================================================================

// Variant 4.1: %Ns for bounded string read
void test_bounded_string() {
    char buf[10];
    if (scanf("%9s", buf) == 1) {  // %9s reads max 9 chars + null
        // OK: bounded read
        (void)buf;
    }
}

// Variant 4.2: %Nc for exact char count
void test_bounded_chars() {
    char buf[5];
    if (scanf("%4c", buf) == 1) {  // Read exactly 4 chars
        buf[4] = '\0';  // Manual null terminate
        (void)buf;
    }
}

// =============================================================================
// SECTION 5: Check EOF
// =============================================================================

// Variant 5.1: check for EOF
void test_check_eof() {
    int x;
    int result = scanf("%d", &x);
    if (result == EOF) {
        // Handle EOF
    } else if (result == 1) {
        // OK: x is valid
        (void)x;
    }
}

// =============================================================================
// SECTION 6: Loop with scanf check
// =============================================================================

// Variant 6.1: while loop with scanf check
void test_scanf_loop() {
    int x;
    while (scanf("%d", &x) == 1) {
        // OK: x is initialized each iteration
        (void)x;
    }
}

// =============================================================================
// SECTION 7: scanf with suppression
// =============================================================================

// Variant 7.1: suppress assignment with *
void test_scanf_suppress() {
    int x;
    // Skip a word, then read int
    if (scanf("%*s %d", &x) == 1) {  // Only counts non-suppressed
        // OK: x is initialized
        (void)x;
    }
}

// =============================================================================
// SECTION 8: Partial success handling
// =============================================================================

// Variant 8.1: handle partial reads
void test_partial_read() {
    int a, b;
    int count = scanf("%d %d", &a, &b);
    if (count >= 1) {
        // OK: at least a is initialized
        (void)a;
    }
    if (count >= 2) {
        // OK: both initialized
        (void)b;
    }
}

// =============================================================================
// SECTION 9: scanf_s (secure variant where available)
// =============================================================================

// Note: scanf_s is Microsoft-specific, but the pattern applies
// Variant 9.1: bounded buffer with size
void test_bounded_buffer() {
    char buf[100];
    // Using %99s is the portable way to bound input
    if (scanf("%99s", buf) == 1) {
        size_t len = strlen(buf);  // OK: null terminated
        (void)len;
    }
}

// =============================================================================
// SECTION 10: Initialize before scanf (defensive)
// =============================================================================

// Variant 10.1: pre-initialize variable
void test_pre_init() {
    int x = 0;  // Pre-initialized
    scanf("%d", &x);  // Even if fails, x has known value
    // Can use x (it's 0 or the scanned value)
    (void)x;
}

int main() {
    return 0;
}
