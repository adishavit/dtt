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
    guarded_close(f);  // ERROR: f._nonnull not proven (fopen can fail)  dtt-error {{_nonnull != 0}}
}

// Variant 2: Alloc without null check
void variant2_alloc_no_check() {
    void* p = guarded_alloc(100);
    guarded_dealloc(p);  // ERROR: p._nonnull not proven  dtt-error {{_nonnull != 0}}
}

// Variant 3: Open file, never null-check the result
// Resource pointer is never checked — potential leak
void variant3_unused_nullable_result() {
    TestFile* f = guarded_open("data.txt");
    // f could be null — never checked
    // ERROR: resource pointer never null-checked (potential leak)
}

// Variant 4: Allocate memory, never null-check the result
// Resource pointer is never checked — potential leak
void variant4_alloc_unused() {
    void* p = guarded_alloc(100);
    // p might be null — never checked
    // ERROR: resource pointer never null-checked (potential leak)
}
