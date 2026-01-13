// 708_static_function_params_pass.cpp
// Tests for call-site analysis of static functions
// When ALL callers of a static function pass non-null to a dereferenced parameter,
// DTT should NOT warn at the function definition.

// =============================================================================
// Test 1: Simple static function with safe caller (address-of)
// =============================================================================
static int deref_ptr(int* p) { return *p; }

void test1_safe_caller() {
    int x = 42;
    int val = deref_ptr(&x);  // SAFE: &x is non-null
    (void)val;
}

// =============================================================================
// Test 2: Multiple safe callers
// =============================================================================
static int sum_pair(int* a, int* b) { return *a + *b; }

void test2_caller1() {
    int x = 1, y = 2;
    int sum = sum_pair(&x, &y);  // SAFE: both address-of
    (void)sum;
}

void test2_caller2() {
    int arr[] = {3, 4};
    int sum = sum_pair(&arr[0], &arr[1]);  // SAFE: address-of array elements
    (void)sum;
}

// =============================================================================
// Test 3: String literal caller
// =============================================================================
static int first_char(const char* s) { return s[0]; }

void test3_string_literal() {
    int c = first_char("hello");  // SAFE: string literal is non-null
    (void)c;
}

// =============================================================================
// Test 4: Array decay caller
// =============================================================================
static int array_first(const int* arr) { return arr[0]; }

void test4_array_decay() {
    int nums[] = {1, 2, 3};
    int val = array_first(nums);  // SAFE: array decay is non-null
    (void)val;
}

// =============================================================================
// Test 5: Static function in namespace
// =============================================================================
namespace ns {
    static void set_value(int* p) { *p = 100; }
}

void test5_namespace_static() {
    int x;
    ns::set_value(&x);  // SAFE
    (void)x;
}

// =============================================================================
// Test 6: Anonymous namespace (internal linkage)
// =============================================================================
namespace {
    void anon_deref(int* p) { *p = 200; }
}

void test6_anonymous_namespace() {
    int x;
    anon_deref(&x);  // SAFE
    (void)x;
}

// =============================================================================
// Test 7: Chained static calls - all callers safe
// =============================================================================
static void inner_func(int* p) { *p = 1; }
static void outer_func(int* p) { inner_func(p); }

void test7_chained_safe() {
    int x;
    outer_func(&x);  // SAFE: propagates through chain
    (void)x;
}

// =============================================================================
// Test 8: Static with guarded heap - caller guards before calling
// =============================================================================
static void use_buffer(char* buf) { buf[0] = 'x'; }

void test8_guarded_malloc() {
    char* p = (char*)__builtin_malloc(10);
    if (p) {
        use_buffer(p);  // SAFE: guarded by if(p)
        __builtin_free(p);
    }
}

// =============================================================================
// Test 9: new expression (throws on failure, always non-null)
// =============================================================================
static void set_int(int* p) { *p = 42; }

void test9_new_expression() {
    int* p = new int;
    set_int(p);  // SAFE: new throws on failure
    delete p;
}

int main() {
    test1_safe_caller();
    test2_caller1();
    test2_caller2();
    test3_string_literal();
    test4_array_decay();
    test5_namespace_static();
    test6_anonymous_namespace();
    test7_chained_safe();
    test8_guarded_malloc();
    test9_new_expression();
    return 0;
}
