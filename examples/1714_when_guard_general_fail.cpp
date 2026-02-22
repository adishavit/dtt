// Test: Generalized `when` guards — FAILURE cases
// These should produce DTT errors (precondition violations).

struct TestFile {};
struct GContainer {};

TestFile* guarded_sopen(const char* path);
void guarded_sclose(TestFile* f);

// Variant 1: close without null-check → precondition error
void variant1_close_without_check() {
    TestFile* f = guarded_sopen("data.txt");
    guarded_sclose(f);  // ERROR: arg0._nonnull not proven
}

// Variant 2: scope_exit guard — unused nullable result
// Resource pointer is never null-checked — potential leak
void variant2_scope_exit_nullable() {
    TestFile* f = guarded_sopen("data.txt");
    // f might be null — no null check
    // ERROR: resource pointer never null-checked (potential leak)
}
