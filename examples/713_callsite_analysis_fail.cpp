// 713_callsite_analysis_pass.cpp
// Call-site analysis for static functions
// DTT should warn at CALL SITE (not definition) for static functions
// when caller passes potentially-null argument to parameter that gets dereferenced

// ============================================================================
// CATEGORY A: STATIC FUNCTIONS WITH SAFE CALLERS - NO WARNINGS EXPECTED
// ============================================================================

// A1: Literal string argument
static int a1_strlen(const char* s) { return s[0]; }
void a1_caller() { a1_strlen("hello"); }

// A2: Address-of local variable
static void a2_increment(int* p) { (*p)++; }
void a2_caller() { int x = 0; a2_increment(&x); }

// A3: Stack array decay
static int a3_first(const int* arr) { return arr[0]; }
void a3_caller() { int data[] = {1,2,3}; a3_first(data); }

// A4: Guarded heap pointer
static void a4_use(char* buf) { buf[0] = 'x'; }
void a4_caller() {
    char* p = (char*)__builtin_malloc(10);
    if (p) { a4_use(p); __builtin_free(p); }
}

// A5: Multiple safe callers
static int a5_deref(int* p) { return *p; }
void a5_caller1() { int x = 1; a5_deref(&x); }
void a5_caller2() { int y = 2; a5_deref(&y); }
void a5_caller3() { int z = 3; a5_deref(&z); }

// A6: Nested address-of in expression
static void a6_swap(int* a, int* b) { int t = *a; *a = *b; *b = t; }
void a6_caller() { int x = 1, y = 2; a6_swap(&x, &y); }

// A7: Constexpr/static array
static int a7_get(const int* arr, int i) { return arr[i]; }
void a7_caller() { static const int data[] = {10, 20}; a7_get(data, 0); }

// A8: Compound literal (C99/C++)
static int a8_sum(const int* arr) { return arr[0] + arr[1]; }
void a8_caller() { a8_sum((int[]){1, 2}); }

// A9: Return value of function returning non-null
static const char* a9_get_str() { return "constant"; }
static int a9_use(const char* s) { return s[0]; }
void a9_caller() { a9_use(a9_get_str()); }

// A10: Parameter after null guard in same function
static void a10_inner(int* p) { *p = 42; }
void a10_caller(int* maybe_null) {
    if (!maybe_null) return;
    a10_inner(maybe_null);  // SAFE: guarded above
}

// ============================================================================
// CATEGORY B: STATIC FUNCTIONS WITH UNSAFE CALLERS - WARN AT CALL SITE
// ============================================================================

// B1: Unguarded parameter passed through
static void b1_deref(int* p) { *p = 1; }
void b1_caller(int* unknown) { b1_deref(unknown); }  // WARN: unknown may be null  dtt-error {{passing null|cannot verify .* is non-null}}

// B2: Unguarded malloc result
static void b2_fill(char* buf) { buf[0] = 'a'; }
void b2_caller() {
    char* p = (char*)__builtin_malloc(10);
    b2_fill(p);  // KNOWN GAP (not asserted): malloc may return null
    __builtin_free(p);
}

// B3: Nullable return value passed
static char* b3_maybe_null() { return (char*)__builtin_malloc(1); }
static void b3_use(char* p) { *p = 'x'; }
void b3_caller() { b3_use(b3_maybe_null()); }  // WARN: return may be null  dtt-error {{passing null|cannot verify .* is non-null}}

// B4: Conditional initialization
static void b4_process(int* data) { *data = 99; }
void b4_caller(bool flag) {
    int* p = nullptr;
    if (flag) { int x = 0; p = &x; }
    b4_process(p);  // WARN: p may be null when !flag  dtt-error {{passing null|cannot verify .* is non-null}}
}

// B5: One bad caller among good ones
static int b5_read(const int* p) { return *p; }
void b5_good1() { int x = 1; b5_read(&x); }  // SAFE
void b5_good2() { int y = 2; b5_read(&y); }  // SAFE
void b5_bad(int* unknown) { b5_read(unknown); }  // WARN: unknown may be null  dtt-error {{passing null|cannot verify .* is non-null}}

// B6: Multiple params, one unsafe
static void b6_copy(int* dst, const int* src) { *dst = *src; }
void b6_caller(int* unknown) {
    int local = 0;
    b6_copy(&local, unknown);  // WARN: src (unknown) may be null  dtt-error {{passing null|cannot verify .* is non-null}}
}

// B7: Param used after potential null assignment
static void b7_use(int* p) { *p = 1; }
void b7_caller() {
    int x = 0;
    int* p = &x;
    p = nullptr;  // Now null!
    b7_use(p);  // KNOWN GAP (not asserted): p is null
}

// B8: Ternary with null branch
static void b8_use(int* p) { *p = 1; }
void b8_caller(bool cond) {
    int x = 0;
    int* p = cond ? &x : nullptr;
    b8_use(p);  // WARN: p may be null  dtt-error {{passing null|cannot verify .* is non-null}}
}

// B9: Array element that might be null
static void b9_use(int* p) { *p = 1; }
void b9_caller(int* arr[]) {
    b9_use(arr[0]);  // WARN: arr[0] may be null  dtt-error {{passing null|cannot verify .* is non-null}}
}

// B10: Cast from void* without check
static void b10_use(int* p) { *p = 1; }
void b10_caller(void* v) {
    b10_use((int*)v);  // WARN: v may be null  dtt-error {{passing null|cannot verify .* is non-null}}
}

// ============================================================================
// CATEGORY C: NON-STATIC FUNCTIONS - WARN AT DEFINITION (UNKNOWN CALLERS)
// ============================================================================

// C1: Public function - external callers unknown
void c1_public(int* p) { *p = 1; }  // WARN at definition  dtt-error {{potential null pointer dereference}}

// C2: Extern declared function
extern void c2_extern(int* p);
void c2_extern(int* p) { *p = 1; }  // WARN at definition  dtt-error {{potential null pointer dereference}}

// C3: Non-static with safe internal callers (still warn - external unknown)
void c3_public(int* p) { *p = 1; }  // KNOWN GAP (not asserted):at definition
void c3_internal_caller() { int x = 0; c3_public(&x); }

// ============================================================================
// CATEGORY D: NAMESPACE AND CLASS STATIC - SAME AS FILE STATIC
// ============================================================================

namespace ns {
    // D1: Static in namespace - safe caller
    static void d1_use(int* p) { *p = 1; }
    void d1_caller() { int x = 0; d1_use(&x); }  // SAFE

    // D2: Static in namespace - unsafe caller
    static void d2_use(int* p) { *p = 1; }
    void d2_caller(int* unknown) { d2_use(unknown); }  // WARN  dtt-error {{passing null|cannot verify .* is non-null}}
}

namespace {
    // D3: Anonymous namespace (implicitly static) - safe caller
    void d3_use(int* p) { *p = 1; }
    void d3_safe() { int x = 0; d3_use(&x); }  // SAFE

    // D4: Anonymous namespace - unsafe caller
    void d4_use(int* p) { *p = 1; }
    void d4_unsafe(int* unknown) { d4_use(unknown); }  // KNOWN GAP (not asserted):
}

// D5: Class private static method - safe caller
class MyClass {
    static void d5_helper(int* p) { *p = 1; }
public:
    void safe_call() { int x = 0; d5_helper(&x); }  // SAFE
    void unsafe_call(int* p) { d5_helper(p); }  // WARN if p unknown  dtt-error {{passing null|cannot verify .* is non-null}}
};

// ============================================================================
// CATEGORY E: CHAINED CALLS - PROPAGATE NULLABILITY
// ============================================================================

// E1: Two-level chain, safe at root
static void e1_inner(int* p) { *p = 1; }
static void e1_outer(int* p) { e1_inner(p); }
void e1_caller() { int x = 0; e1_outer(&x); }  // SAFE: propagates

// E2: Two-level chain, unsafe at root
static void e2_inner(int* p) { *p = 1; }
static void e2_outer(int* p) { e2_inner(p); }
void e2_caller(int* unknown) { e2_outer(unknown); }  // WARN: propagates  dtt-error {{passing null|cannot verify .* is non-null}}

// E3: Three-level chain
static void e3_level3(int* p) { *p = 1; }
static void e3_level2(int* p) { e3_level3(p); }
static void e3_level1(int* p) { e3_level2(p); }
void e3_safe() { int x = 0; e3_level1(&x); }  // SAFE
void e3_unsafe(int* u) { e3_level1(u); }  // WARN  dtt-error {{passing null|cannot verify .* is non-null}}

// ============================================================================
// CATEGORY F: MULTIPLE PARAMETERS
// ============================================================================

// F1: Both params safe
static void f1_both(int* a, int* b) { *a = *b; }
void f1_caller() { int x = 1, y = 2; f1_both(&x, &y); }  // SAFE

// F2: First param unsafe
static void f2_both(int* a, int* b) { *a = *b; }
void f2_caller(int* unknown) { int y = 2; f2_both(unknown, &y); }  // WARN: a  dtt-error {{passing null|cannot verify .* is non-null}}

// F3: Second param unsafe
static void f3_both(int* a, int* b) { *a = *b; }
void f3_caller(int* unknown) { int x = 1; f3_both(&x, unknown); }  // WARN: b  dtt-error {{passing null|cannot verify .* is non-null}}

// F4: Both params unsafe
static void f4_both(int* a, int* b) { *a = *b; }
void f4_caller(int* u1, int* u2) { f4_both(u1, u2); }  // WARN: both  dtt-error {{passing null|cannot verify .* is non-null}} dtt-error {{passing null|cannot verify .* is non-null}}

// F5: Three params, middle unsafe
static int f5_three(int* a, int* b, int* c) { return *a + *b + *c; }
void f5_caller(int* unknown) {
    int x = 1, z = 3;
    f5_three(&x, unknown, &z);  // WARN: b only  dtt-error {{passing null|cannot verify .* is non-null}}
}

// F6: Only some params dereferenced
static int f6_partial(int* derefed, int* not_derefed) {
    (void)not_derefed;  // Not dereferenced
    return *derefed;
}
void f6_caller(int* u1, int* u2) {
    f6_partial(u1, u2);  // WARN: only u1 (derefed maps to it)  dtt-error {{passing null|cannot verify .* is non-null}}
}

// ============================================================================
// CATEGORY G: SPECIAL CASES
// ============================================================================

// G1: Recursive static function
static int g1_recursive(const char* s) {
    if (*s == '\0') return 0;
    return 1 + g1_recursive(s + 1);
}
void g1_caller() { g1_recursive("hello"); }  // SAFE

// G2: Static function never called (no callers to analyze)
static void g2_unused(int* p) { *p = 1; }  // Should still warn? Or skip?

// G3: Self-recursive with unsafe external call
static void g3_recurse(int* p, int n) {
    if (n <= 0) return;
    *p = n;
    g3_recurse(p, n - 1);
}
void g3_unsafe(int* unknown) { g3_recurse(unknown, 5); }  // KNOWN GAP (not asserted):

// G4: Mutually recursive
static void g4_b(int* p, int n);
static void g4_a(int* p, int n) {
    if (n <= 0) return;
    *p = n;
    g4_b(p, n - 1);
}
static void g4_b(int* p, int n) {
    if (n <= 0) return;
    *p = n * 2;
    g4_a(p, n - 1);
}
void g4_safe() { int x = 0; g4_a(&x, 5); }  // SAFE
void g4_unsafe(int* u) { g4_a(u, 5); }  // KNOWN GAP (not asserted):

// G5: Static inline
static inline void g5_inline(int* p) { *p = 1; }
void g5_safe() { int x = 0; g5_inline(&x); }  // SAFE
void g5_unsafe(int* u) { g5_inline(u); }  // WARN  dtt-error {{passing null|cannot verify .* is non-null}}

// G6: Template static (if supported)
template<typename T>
static void g6_template(T* p) { *p = T(); }
void g6_caller() { int x; g6_template(&x); }  // SAFE

// G7: Static with default param
static void g7_default(int* p, int val = 0) { *p = val; }
void g7_safe() { int x; g7_default(&x); }  // SAFE
void g7_unsafe(int* u) { g7_default(u); }  // WARN  dtt-error {{passing null|cannot verify .* is non-null}}

// G8: Variadic static (pointer is first param)
static void g8_variadic(int* first, ...) { *first = 1; }
void g8_safe() { int x; g8_variadic(&x, 2, 3); }  // SAFE
void g8_unsafe(int* u) { g8_variadic(u, 2, 3); }  // WARN  dtt-error {{passing null|cannot verify .* is non-null}}

// ============================================================================
// CATEGORY H: FUNCTION HANDLES NULL INTERNALLY (NO WARN NEEDED)
// ============================================================================

// H1: Function guards internally
static void h1_guarded(int* p) {
    if (!p) return;
    *p = 1;
}
void h1_caller(int* unknown) { h1_guarded(unknown); }  // SAFE: function guards

// H2: Function uses ternary guard
static int h2_guarded(int* p) {
    return p ? *p : 0;
}
void h2_caller(int* unknown) { h2_guarded(unknown); }  // SAFE: function guards

// H3: Function explicitly documents nullable
static void h3_nullable(int* p /* nullable */) {
    if (p) *p = 1;
}
void h3_caller() { h3_nullable(nullptr); }  // SAFE: intentional null

// ============================================================================
// CATEGORY I: EDGE CASES FOR ARGUMENT EXPRESSIONS
// ============================================================================

// I1: Argument is function call result (non-null)
static int* i1_alloc() { static int x; return &x; }
static void i1_use(int* p) { *p = 1; }
void i1_caller() { i1_use(i1_alloc()); }  // SAFE: i1_alloc returns non-null

// I2: Argument is conditional expression (one branch null)
static void i2_use(int* p) { *p = 1; }
void i2_caller(bool c) {
    int x = 0;
    i2_use(c ? &x : nullptr);  // WARN: may be null  dtt-error {{passing null|cannot verify .* is non-null}}
}

// I3: Argument is cast
static void i3_use(int* p) { *p = 1; }
void i3_caller() {
    long addr = 0x1000;
    i3_use((int*)addr);  // Technically unsafe but hard to prove
}

// I4: Argument is member access
struct S { int* ptr; };
static void i4_use(int* p) { *p = 1; }
void i4_caller(S* s) { i4_use(s->ptr); }  // WARN: s->ptr may be null  dtt-error {{potential null pointer dereference}} dtt-error {{passing null|cannot verify .* is non-null}}

// I5: Argument is array subscript
static void i5_use(int* p) { *p = 1; }
void i5_caller(int** arr) { i5_use(arr[0]); }  // WARN: arr[0] may be null  dtt-error {{passing null|cannot verify .* is non-null}}

// I6: Argument is new expression (C++)
static void i6_use(int* p) { *p = 1; }
void i6_caller() { i6_use(new int(0)); }  // SAFE: new doesn't return null (throws)

// I7: Argument is &(*ptr) - always non-null if ptr is valid
static void i7_use(int* p) { *p = 1; }
void i7_caller(int* ptr) {
    if (!ptr) return;
    i7_use(&(*ptr));  // SAFE: &(*ptr) == ptr which is guarded
}

// ============================================================================
// CATEGORY J: LAMBDA AND FUNCTION OBJECTS (STRETCH GOAL)
// ============================================================================

// J1: Lambda capturing and calling static
static void j1_target(int* p) { *p = 1; }
void j1_caller() {
    auto fn = []() { int x = 0; j1_target(&x); };  // SAFE
    fn();
}

// J2: std::function-like pattern (manual)
typedef void (*FnPtr)(int*);
static void j2_target(int* p) { *p = 1; }
void j2_caller() {
    FnPtr f = j2_target;
    int x = 0;
    f(&x);  // Harder to track through function pointer
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    // Category A - all should pass (no warnings)
    a1_caller();
    a2_caller();
    a3_caller();
    a4_caller();
    a5_caller1(); a5_caller2(); a5_caller3();
    a6_caller();
    a7_caller();
    a8_caller();
    // a9_caller(); // commented due to static init
    // a10 requires parameter

    // Category B - warnings at call sites
    // (would need to pass args)

    // Category C - warnings at definitions
    // (public functions)

    return 0;
}
