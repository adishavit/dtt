// Test: Conditional postconditions with `when` guards in .dtt annotations
// The `when` guard makes postconditions conditional on runtime state.
//
// Without `when`: `ensures result.@open = 1` sets @open unconditionally,
// causing false-positive leak warnings even when fopen returns NULL.
//
// With `when`: `ensures result.@open = 1 when result._nonnull != 0`
// only sets @open when the result is non-null — no false positive.

struct TestFile {};

// Annotated via annotations/test_when.dtt:
//   fn guarded_open(1) {
//     ensures result.@open = 1 when result._nonnull != 0
//     scope_exit result.@open == 0
//   }
//   fn guarded_close(1) {
//     requires arg0._nonnull != 0 "handle may be null"
//     ensures arg0.@open = 0
//     ensures arg0._nonnull = 0
//   }
TestFile* guarded_open(const char* path);
void guarded_close(TestFile* f);

void* guarded_alloc(int size);
void guarded_dealloc(void* p);

// Variant 1: Open file, null-check, close — standard pattern
void variant1_null_checked_open() {
    TestFile* f = guarded_open("data.txt");
    if (f) {
        guarded_close(f);
    }
    // If f was null: @open never set (guard not satisfied), no leak
    // If f was non-null: @open set to 1, then 0 by close, no leak
}

// Variant 2: Open file, don't use it at all
// Without `when`: FALSE POSITIVE — @open = 1 set unconditionally, scope_exit fails
// With `when`: OK — f is never proven non-null, so @open is never set to 1
void variant2_unused_nullable_result() {
    TestFile* f = guarded_open("data.txt");
    // f could be null — never checked
    // With `when` guard: @open NOT set (guard condition not met)
    // No scope_exit violation
}

// Variant 3: Early return on null, close on success
void variant3_early_return() {
    TestFile* f = guarded_open("data.txt");
    if (!f) return;  // null path: @open never set, clean exit
    guarded_close(f);
}

// Variant 4: Multiple resources, independent null checks
void variant4_multiple_resources() {
    TestFile* f1 = guarded_open("a.txt");
    TestFile* f2 = guarded_open("b.txt");
    if (f1) guarded_close(f1);
    if (f2) guarded_close(f2);
    // Each independently null-checked; unchecked paths have no @open set
}

// Variant 5: Allocate memory, same pattern
void variant5_alloc_unused() {
    void* p = guarded_alloc(100);
    // p might be null — never checked
    // With `when` guard: @mem NOT set, no scope_exit violation
}
