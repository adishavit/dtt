// 710_uninitialized_scalar_pass.cpp
// Safe patterns that should compile without DTT errors
// These demonstrate proper initialization before use

// =============================================================================
// Properly Initialized Scalars
// =============================================================================

// Pattern 1: Direct initialization
int pattern1_direct_init() {
    int x = 0;
    return x;
}

// Pattern 2: Value initialization
int pattern2_value_init() {
    int x{};         // Value-initialized to 0
    return x;
}

// Pattern 3: Copy initialization
int pattern3_copy_init() {
    int src = 42;
    int dst = src;
    return dst;
}

// Pattern 4: Assignment before use
int pattern4_assign_before_use() {
    int x;
    x = 100;
    return x;
}

// Pattern 5: Multiple assignments
int pattern5_multiple_assign() {
    int x;
    x = 1;
    x = 2;
    x = 3;
    return x;
}

// =============================================================================
// Safe Expression Patterns
// =============================================================================

// Pattern 6: Initialized in expression
int pattern6_init_expr() {
    int x = 10;
    int y = x + 5;
    return y;
}

// Pattern 7: Chained initialization
int pattern7_chained() {
    int a = 1;
    int b = a * 2;
    int c = b + a;
    return c;
}

// Pattern 8: Unary on initialized
int pattern8_unary_init() {
    int x = 5;
    return -x;
}

// Pattern 9: Increment on initialized
int pattern9_increment_init() {
    int x = 0;
    return ++x;
}

// Pattern 10: Post-increment on initialized
int pattern10_post_increment_init() {
    int x = 0;
    return x++;
}

// =============================================================================
// Safe Function Call Patterns
// =============================================================================

void sink(int x);

// Pattern 11: Pass initialized value
void pattern11_pass_init() {
    int x = 42;
    sink(x);
}

// Pattern 12: Pass address of uninitialized (output param)
void pattern12_address_of() {
    int x;
    int* p = &x;     // Taking address is OK
    (void)p;
}

// =============================================================================
// Safe Control Flow Patterns
// =============================================================================

// Pattern 14: Initialized in all branches
int pattern14_all_branches(bool cond) {
    int x;
    if (cond) {
        x = 1;
    } else {
        x = 2;
    }
    return x;        // OK: initialized on all paths
}

// Pattern 15: Early return after init
int pattern15_early_return(bool cond) {
    int x = 0;
    if (cond) {
        return x;    // OK: x is initialized
    }
    x = 42;
    return x;        // OK: x is initialized
}

// Pattern 16: Loop with initialization
int pattern16_loop_init() {
    int sum = 0;     // Initialized before loop
    for (int i = 0; i < 10; i++) {
        sum += i;    // OK: sum was initialized
    }
    return sum;
}

// =============================================================================
// Safe Compound Assignment Patterns
// =============================================================================

// Pattern 17: Compound on initialized
int pattern17_compound_init() {
    int x = 10;
    x += 5;
    return x;
}

// Pattern 18: Multiple compound ops
int pattern18_multi_compound() {
    int x = 1;
    x *= 2;
    x += 3;
    x -= 1;
    return x;
}

// =============================================================================
// Non-Evaluating Contexts (Safe)
// =============================================================================

// Pattern 19: sizeof doesn't evaluate
int pattern19_sizeof() {
    int x;
    return sizeof(x);  // OK: sizeof doesn't read value
}

// Pattern 20: decltype doesn't evaluate
int pattern20_decltype() {
    int x;
    decltype(x) y = 0; // OK: decltype doesn't read value
    return y;
}

// Pattern 21: alignof doesn't evaluate
int pattern21_alignof() {
    int x;
    return alignof(decltype(x)); // OK: doesn't read value
}

// Pattern 22: Address-of doesn't read
void pattern22_address() {
    int x;
    void* p = &x;    // OK: taking address doesn't read
    (void)p;
}

// =============================================================================
// Static/Global (Zero-Initialized)
// =============================================================================

static int global_static;  // Zero-initialized

int pattern23_static() {
    return global_static;  // OK: static storage is zero-initialized
}

int pattern24_local_static() {
    static int x;    // Zero-initialized
    return x;        // OK
}

// =============================================================================
// Parameters (Caller-Initialized)
// =============================================================================

int pattern25_parameter(int x) {
    return x;        // OK: parameters are initialized by caller
}

int pattern26_param_expr(int a, int b) {
    return a + b;    // OK: parameters are initialized
}

int main() {
    pattern1_direct_init();
    pattern4_assign_before_use();
    pattern11_pass_init();
    pattern14_all_branches(true);
    return 0;
}
