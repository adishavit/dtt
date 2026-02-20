// Test: Generalized `when` guards across ALL annotation contexts
// This tests that `when` guards work uniformly in:
//   1. fn block ensures (already tested in 1713)
//   2. fn block scope_exit (NEW)
//   3. type/trait block ensures (NEW)

struct TestFile {};
struct GContainer {
    void prepare();
    void add(int value);
};

// -- fn block functions --
TestFile* guarded_open(const char* path);
void guarded_close(TestFile* f);
void* guarded_alloc(int size);
void guarded_dealloc(void* p);

// -- scope_exit guarded functions --
TestFile* guarded_sopen(const char* path);
void guarded_sclose(TestFile* f);

// ============================================================
// SCOPE_EXIT WHEN GUARDS
// ============================================================

// Variant 1: scope_exit guard — unused nullable result
// Without scope_exit guard: @sopen never set, scope_exit sees field missing
// With scope_exit guard: scope_exit skipped because _nonnull not proven
void variant1_scope_exit_nullable() {
    TestFile* f = guarded_sopen("data.txt");
    // f might be null — no null check
    // ensures guard: _nonnull not proven → @sopen NOT set
    // scope_exit guard: _nonnull not proven → constraint skipped
    // No false positive leak warning!
}

// Variant 2: scope_exit guard — null-checked and properly closed
void variant2_scope_exit_checked() {
    TestFile* f = guarded_sopen("data.txt");
    if (f) {
        // _nonnull proven → ensures fires → @sopen = 1
        // scope_exit guard would be satisfied here
        guarded_sclose(f);
        // @sopen = 0 after close — constraint satisfied
    }
    // null path: guard not satisfied → scope_exit skipped
}

// Variant 3: scope_exit guard — early return on null
void variant3_scope_exit_early_return() {
    TestFile* f = guarded_sopen("data.txt");
    if (!f) return;  // null path exits, guard not satisfied → no leak
    guarded_sclose(f);
}

// ============================================================
// TYPE/TRAIT METHOD WHEN GUARDS
// ============================================================

// Variant 4: type method guard — add without prepare
// _ready = 0 after ctor, so guard `when _ready != 0` is NOT satisfied
// → _size stays at 0 (not incremented)
void variant4_add_without_prepare() {
    GContainer c;
    // c._size = 0, c._ready = 0
    c.add(42);
    // Guard not satisfied: _ready == 0 → _size NOT updated → stays 0
    // No error — this is by design (add is a no-op when not ready)
}

// Variant 5: type method guard — prepare then add
// _ready = 1 after prepare(), guard satisfied → _size incremented
void variant5_prepare_then_add() {
    GContainer c;
    // c._size = 0, c._ready = 0
    c.prepare();
    // c._ready = 1
    c.add(42);
    // Guard satisfied: _ready == 1 → _size = old(_size) + 1 = 1
    c.add(43);
    // Guard satisfied: _ready == 1 → _size = old(_size) + 1 = 2
}
