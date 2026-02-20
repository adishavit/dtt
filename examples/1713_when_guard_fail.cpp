// Test: Conditional postconditions with `when` guards — FAILURE cases
// These should produce DTT errors because preconditions are violated.

struct TestFile {};
TestFile* guarded_open(const char* path);
void guarded_close(TestFile* f);
void* guarded_alloc(int size);
void guarded_dealloc(void* p);

// Variant 1: Use without null check — _nonnull not proven
// guarded_close requires arg0._nonnull != 0, but fopen can return NULL
void variant1_no_null_check() {
    TestFile* f = guarded_open("data.txt");
    guarded_close(f);  // ERROR: f._nonnull not proven (fopen can fail)
}

// Variant 2: Alloc without null check
void variant2_alloc_no_check() {
    void* p = guarded_alloc(100);
    guarded_dealloc(p);  // ERROR: p._nonnull not proven
}
