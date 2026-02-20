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
