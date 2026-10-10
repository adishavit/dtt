// 1203_c_varargs_fail.cpp
// Tests: Unsafe C-style variadic function (varargs) usage patterns
// DTT should catch these — uninitialized arguments passed through ellipsis (...)

// =============================================================================
// SECTION 1: Uninitialized scalars passed to C varargs
// =============================================================================

int printf(const char*, ...);

// Variant 1.1: uninitialized int to printf
void variant1_uninit_int() {
    int x;
    printf("%d", x);  // ERROR: x is uninitialized  dtt-error {{uninitialized variable 'x'}}
}

// Variant 1.2: uninitialized double to printf
void variant2_uninit_double() {
    double d;
    printf("%f", d);  // ERROR: d is uninitialized  dtt-error {{uninitialized variable 'd'}}
}

// Variant 1.3: uninitialized char to printf
void variant3_uninit_char() {
    char c;
    printf("%c", c);  // ERROR: c is uninitialized  dtt-error {{uninitialized variable 'c'}}
}

// Variant 1.4: multiple uninitialized args
void variant4_multi_uninit() {
    int a, b, c;
    printf("%d %d %d", a, b, c);  // ERROR: a, b, c all uninitialized  dtt-error {{uninitialized variable 'a'}} dtt-error {{uninitialized variable 'b'}} dtt-error {{uninitialized variable 'c'}}
}

// Variant 1.5: mix of initialized and uninitialized
void variant5_mixed() {
    int good = 42;
    int bad;
    printf("%d %d", good, bad);  // ERROR: bad is uninitialized  dtt-error {{uninitialized variable 'bad'}}
}

// =============================================================================
// SECTION 2: Uninitialized in custom varargs functions
// =============================================================================

int sum(int count, ...);

// Variant 2.1: uninitialized passed to custom varargs
void variant6_custom_varargs() {
    int x;
    sum(1, x);  // ERROR: x is uninitialized  dtt-error {{uninitialized variable 'x'}}
}

// Variant 2.2: uninitialized among valid args
void variant7_mixed_custom() {
    int a = 10;
    int b;
    int c = 30;
    sum(3, a, b, c);  // ERROR: b is uninitialized  dtt-error {{uninitialized variable 'b'}}
}

// =============================================================================
// SECTION 3: Uninitialized through expressions to varargs
// =============================================================================

// Variant 3.1: uninitialized in arithmetic passed to varargs
void variant8_expr_uninit() {
    int x;
    printf("%d", x + 1);  // ERROR: x is uninitialized (used in expression)  dtt-error {{uninitialized variable 'x'}}
}

// Variant 3.2: uninitialized in cast to varargs
void variant9_cast_uninit() {
    int x;
    printf("%u", (unsigned)x);  // KNOWN GAP (not asserted): x is uninitialized
}

// Variant 3.3: uninitialized negation
void variant10_negate_uninit() {
    int x;
    printf("%d", -x);  // ERROR: x is uninitialized  dtt-error {{uninitialized variable 'x'}}
}

// =============================================================================
// SECTION 4: Conditionally uninitialized to varargs
// =============================================================================

// Variant 4.1: only one branch initializes
void variant11_one_branch(bool cond) {
    int x;
    if (cond) {
        x = 42;
    }
    // x may be uninitialized if !cond
    printf("%d", x);  // KNOWN GAP (not asserted): x may be uninitialized
}

// Variant 4.2: uninitialized pointer to varargs
void variant12_uninit_pointer() {
    int* p;
    printf("%p", (void*)p);  // KNOWN GAP (not asserted): p is uninitialized
}

// =============================================================================
// SECTION 5: Compound uninitialized to varargs
// =============================================================================

// Variant 5.1: compound assignment then varargs
void variant13_compound_uninit() {
    int x;
    x += 1;           // ERROR: reads x before init  dtt-error {{uninitialized variable 'x'}}
    printf("%d", x);
}

// Variant 5.2: post-increment uninit to varargs
void variant14_postinc_uninit() {
    int x;
    printf("%d", x++);  // ERROR: x is uninitialized (reads before increment)  dtt-error {{uninitialized variable 'x'}}
}
