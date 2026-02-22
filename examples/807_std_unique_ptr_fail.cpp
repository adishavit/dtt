// 807_std_unique_ptr_fail.cpp
// Tests: Unsafe std::unique_ptr usage that DTT SHOULD catch but doesn't yet

#include <memory>

// =============================================================================
// SECTION 1: Access on default-constructed (null) unique_ptr
// =============================================================================

// Variant 1.1: dereference default-constructed
void test_deref_default() {
    std::unique_ptr<int> ptr;
    *ptr;  // ERROR: _nonnull == false (UB)
}

// Variant 1.2: arrow on default-constructed
void test_arrow_default() {
    struct S { int x; };
    std::unique_ptr<S> ptr;
    ptr->x;  // ERROR: _nonnull == false (UB)
}

// Variant 1.3: array subscript on default-constructed
void test_subscript_default() {
    std::unique_ptr<int[]> arr;
    arr[0];  // ERROR: _nonnull == false (UB)
}

// =============================================================================
// SECTION 2: Access after reset()
// =============================================================================

// Variant 2.1: dereference after reset()
void test_deref_after_reset() {
    auto ptr = std::make_unique<int>(42);
    ptr.reset();
    *ptr;  // ERROR: reset() sets _nonnull = false
}

// Variant 2.2: arrow after reset()
void test_arrow_after_reset() {
    struct S { int x; };
    auto ptr = std::make_unique<S>();
    ptr.reset();
    ptr->x;  // ERROR
}

// Variant 2.3: reset(nullptr) then access
void test_reset_nullptr() {
    auto ptr = std::make_unique<int>(42);
    ptr.reset(nullptr);
    *ptr;  // ERROR: reset(nullptr) sets _nonnull = false
}

// =============================================================================
// SECTION 3: Access after release()
// =============================================================================

// Variant 3.1: dereference after release()
void test_deref_after_release() {
    auto ptr = std::make_unique<int>(42);
    ptr.release();  // ptr is now null
    *ptr;  // ERROR: _nonnull == false after release
}

// Variant 3.2: arrow after release()
void test_arrow_after_release() {
    struct S { int x; };
    auto ptr = std::make_unique<S>();
    ptr.release();
    ptr->x;  // ERROR
}

// =============================================================================
// SECTION 4: Access after move
// =============================================================================

// Variant 4.1: dereference moved-from unique_ptr
void test_deref_moved_from() {
    auto ptr = std::make_unique<int>(42);
    auto other = std::move(ptr);
    *ptr;  // ERROR: ptr is null after move
}

// Variant 4.2: arrow on moved-from unique_ptr
void test_arrow_moved_from() {
    struct S { int x; };
    auto ptr = std::make_unique<S>();
    auto other = std::move(ptr);
    ptr->x;  // ERROR: ptr is null after move
}

// Variant 4.3: array subscript on moved-from
void test_subscript_moved_from() {
    auto arr = std::make_unique<int[]>(10);
    auto other = std::move(arr);
    arr[0];  // ERROR: arr is null after move
}

// =============================================================================
// SECTION 5: Access on parameter unique_ptr
// =============================================================================

// Variant 5.1: dereference parameter unique_ptr
void test_param_deref(std::unique_ptr<int>& ptr) {
    *ptr;  // ERROR: can't prove _nonnull
}

// Variant 5.2: arrow on parameter unique_ptr
void test_param_arrow(std::unique_ptr<std::string>& ptr) {
    ptr->size();  // ERROR: can't prove _nonnull
}

// Variant 5.3: const ref parameter
void test_param_const_ref(const std::unique_ptr<int>& ptr) {
    *ptr;  // ERROR: can't prove _nonnull
}

// =============================================================================
// SECTION 6: Access in wrong branch
// =============================================================================

// Variant 6.1: access in else of if(ptr)
void test_wrong_branch_else() {
    std::unique_ptr<int> ptr;
    if (ptr) {
        // OK here
    } else {
        *ptr;  // ERROR: ptr is null in else branch
    }
}

// Variant 6.2: access in if(!ptr)
void test_wrong_branch_negated() {
    auto ptr = std::make_unique<int>(42);
    if (!ptr) {
        *ptr;  // ERROR: ptr is null in this branch
    }
}

// Variant 6.3: access outside guard scope
void test_outside_guard_scope() {
    auto ptr = std::make_unique<int>(42);
    if (ptr) {
        // OK here
    }
    ptr.reset();
    *ptr;  // ERROR: guard doesn't protect this
}

// =============================================================================
// SECTION 7: Nullptr initialization then access
// =============================================================================

// Variant 7.1: nullptr initialization
void test_nullptr_init() {
    std::unique_ptr<int> ptr = nullptr;
    *ptr;  // ERROR: _nonnull == false
}

// Variant 7.2: nullptr assignment then access
void test_nullptr_assign() {
    auto ptr = std::make_unique<int>(42);
    ptr = nullptr;
    *ptr;  // ERROR: _nonnull == false
}

// =============================================================================
// SECTION 8: Conditional set without full coverage
// =============================================================================

// Variant 8.1: set only in one branch
void test_conditional_set(bool cond) {
    std::unique_ptr<int> ptr;
    if (cond) {
        ptr = std::make_unique<int>(42);
    }
    *ptr;  // ERROR: only non-null if cond was true
}

// Variant 8.2: reset only in one branch
void test_conditional_reset(bool cond) {
    auto ptr = std::make_unique<int>(42);
    if (cond) {
        ptr.reset();
    }
    *ptr;  // ERROR: might be null if cond was true
}

// =============================================================================
// SECTION 9: swap loses tracking
// =============================================================================

// Variant 9.1: swap with null
void test_swap_with_null() {
    auto ptr = std::make_unique<int>(42);
    std::unique_ptr<int> null_ptr;
    ptr.swap(null_ptr);
    *ptr;  // ERROR: ptr now holds null
}

// Variant 9.2: swap with unknown
void test_swap_unknown(std::unique_ptr<int>& other) {
    auto ptr = std::make_unique<int>(42);
    ptr.swap(other);
    *ptr;  // ERROR: can't prove _nonnull after swap
}

// =============================================================================
// SECTION 10: get() doesn't prove nonnull for later access
// =============================================================================

// Variant 10.1: get() check doesn't guard later access
void test_get_no_guard() {
    std::unique_ptr<int> ptr;
    auto raw = ptr.get();  // null
    if (raw) {
        // raw is non-null here, but...
    }
    *ptr;  // ERROR: ptr still not proven non-null
}

// Variant 10.2: get() result used, original not guarded
void test_get_result_used() {
    auto ptr = std::make_unique<int>(42);
    int* raw = ptr.get();
    ptr.reset();
    *ptr;  // ERROR: ptr is null (raw is dangling too)
}

// =============================================================================
// SECTION 11: Return value from function, unguarded
// =============================================================================

std::unique_ptr<int> maybe_create(bool b) {
    if (b) return std::make_unique<int>(42);
    return nullptr;
}

// Variant 11.1: unguarded access to function return
void test_return_unguarded() {
    auto ptr = maybe_create(false);
    *ptr;  // ERROR: might be null
}

// Variant 11.2: chained call unguarded
void test_chained_unguarded() {
    *maybe_create(true);  // ERROR: can't prove non-null at compile time
}

// =============================================================================
// SECTION 12: Copy/move construction from null
// =============================================================================

// Variant 12.1: move from null unique_ptr
void test_move_from_null() {
    std::unique_ptr<int> src;
    auto dest = std::move(src);
    *dest;  // ERROR: dest is null (moved from null)
}

// Variant 12.2: assignment from null
void test_assign_from_null() {
    auto ptr = std::make_unique<int>(42);
    std::unique_ptr<int> null_ptr;
    ptr = std::move(null_ptr);
    *ptr;  // ERROR: ptr is now null
}

// =============================================================================
// SECTION 13: Array bounds (if tracking size)
// =============================================================================

// Variant 13.1: array out of bounds (requires size tracking)
void test_array_oob() {
    auto arr = std::make_unique<int[]>(10);
    arr[100];  // ERROR: 100 >= 10 (if we track array size)
}

// =============================================================================
// SECTION 14: Multiple dereferences with state change
// =============================================================================

// Variant 14.1: access, reset, access
void test_access_reset_access() {
    auto ptr = std::make_unique<int>(42);
    *ptr = 100;  // OK
    ptr.reset();
    *ptr;  // ERROR: second access after reset
}

// Variant 14.2: access, release, access
void test_access_release_access() {
    auto ptr = std::make_unique<int>(42);
    *ptr = 100;  // OK
    auto raw = ptr.release();
    *ptr;  // ERROR: ptr is null after release
    delete raw;  // clean up
}

// =============================================================================
// SECTION 15: Comparison doesn't prove nonnull in else
// =============================================================================

// Variant 15.1: comparison false branch
void test_comparison_else() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr == nullptr) {
        // ptr is null here
    } else {
        // OK here
    }
    // but after the if, we can't be sure
    ptr.reset();
    if (ptr != nullptr) {
        // OK
    } else {
        *ptr;  // ERROR: ptr is null in else
    }
}

int main() {
    return 0;
}
