// 809_std_shared_ptr_xfail.cpp
// Tests: Unsafe std::shared_ptr usage that DTT SHOULD catch but doesn't yet

#include <memory>

// =============================================================================
// SECTION 1: Access on default-constructed (null) shared_ptr
// =============================================================================

// Variant 1.1: dereference default-constructed
void test_deref_default() {
    std::shared_ptr<int> ptr;
    *ptr;  // ERROR: _nonnull == false (UB)
}

// Variant 1.2: arrow on default-constructed
void test_arrow_default() {
    struct S { int x; };
    std::shared_ptr<S> ptr;
    ptr->x;  // ERROR: _nonnull == false (UB)
}

// =============================================================================
// SECTION 2: Access after reset()
// =============================================================================

// Variant 2.1: dereference after reset()
void test_deref_after_reset() {
    auto ptr = std::make_shared<int>(42);
    ptr.reset();
    *ptr;  // ERROR: reset() sets _nonnull = false
}

// Variant 2.2: arrow after reset()
void test_arrow_after_reset() {
    struct S { int x; };
    auto ptr = std::make_shared<S>();
    ptr.reset();
    ptr->x;  // ERROR
}

// Variant 2.3: assignment to nullptr then access
void test_assign_nullptr() {
    auto ptr = std::make_shared<int>(42);
    ptr = nullptr;  // Reset to null
    *ptr;  // ERROR: ptr is now null
}

// =============================================================================
// SECTION 3: Access after move
// =============================================================================

// Variant 3.1: dereference moved-from shared_ptr
void test_deref_moved_from() {
    auto ptr = std::make_shared<int>(42);
    auto other = std::move(ptr);
    *ptr;  // ERROR: ptr is null after move
}

// Variant 3.2: arrow on moved-from shared_ptr
void test_arrow_moved_from() {
    struct S { int x; };
    auto ptr = std::make_shared<S>();
    auto other = std::move(ptr);
    ptr->x;  // ERROR: ptr is null after move
}

// =============================================================================
// SECTION 4: Access on parameter shared_ptr
// =============================================================================

// Variant 4.1: dereference parameter shared_ptr
void test_param_deref(std::shared_ptr<int> ptr) {
    *ptr;  // ERROR: can't prove _nonnull
}

// Variant 4.2: arrow on parameter shared_ptr
void test_param_arrow(std::shared_ptr<std::string>& ptr) {
    ptr->size();  // ERROR: can't prove _nonnull
}

// Variant 4.3: const ref parameter
void test_param_const_ref(const std::shared_ptr<int>& ptr) {
    *ptr;  // ERROR: can't prove _nonnull
}

// =============================================================================
// SECTION 5: Access in wrong branch
// =============================================================================

// Variant 5.1: access in else of if(ptr)
void test_wrong_branch_else() {
    std::shared_ptr<int> ptr;
    if (ptr) {
        // OK here
    } else {
        *ptr;  // ERROR: ptr is null in else branch
    }
}

// Variant 5.2: access in if(!ptr)
void test_wrong_branch_negated() {
    auto ptr = std::make_shared<int>(42);
    if (!ptr) {
        *ptr;  // ERROR: ptr is null in this branch
    }
}

// Variant 5.3: access outside guard scope
void test_outside_guard_scope() {
    auto ptr = std::make_shared<int>(42);
    if (ptr) {
        // OK here
    }
    ptr.reset();
    *ptr;  // ERROR: guard doesn't protect this
}

// =============================================================================
// SECTION 6: Nullptr initialization then access
// =============================================================================

// Variant 6.1: nullptr initialization
void test_nullptr_init() {
    std::shared_ptr<int> ptr = nullptr;
    *ptr;  // ERROR: _nonnull == false
}

// Variant 6.2: nullptr assignment then access
void test_nullptr_assign() {
    auto ptr = std::make_shared<int>(42);
    ptr = nullptr;
    *ptr;  // ERROR: _nonnull == false
}

// =============================================================================
// SECTION 7: Copy/move from null
// =============================================================================

// Variant 7.1: copy from null shared_ptr
void test_copy_from_null() {
    std::shared_ptr<int> src;
    std::shared_ptr<int> dest = src;  // copy from null
    *dest;  // ERROR: dest is null
}

// Variant 7.2: move from null shared_ptr
void test_move_from_null() {
    std::shared_ptr<int> src;
    auto dest = std::move(src);
    *dest;  // ERROR: dest is null (moved from null)
}

// Variant 7.3: assignment from null
void test_assign_from_null() {
    auto ptr = std::make_shared<int>(42);
    std::shared_ptr<int> null_ptr;
    ptr = null_ptr;  // assign null
    *ptr;  // ERROR: ptr is now null
}

// =============================================================================
// SECTION 8: Conditional set without full coverage
// =============================================================================

// Variant 8.1: set only in one branch
void test_conditional_set(bool cond) {
    std::shared_ptr<int> ptr;
    if (cond) {
        ptr = std::make_shared<int>(42);
    }
    *ptr;  // ERROR: only non-null if cond was true
}

// Variant 8.2: reset only in one branch
void test_conditional_reset(bool cond) {
    auto ptr = std::make_shared<int>(42);
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
    auto ptr = std::make_shared<int>(42);
    std::shared_ptr<int> null_ptr;
    ptr.swap(null_ptr);
    *ptr;  // ERROR: ptr now holds null
}

// Variant 9.2: swap with unknown
void test_swap_unknown(std::shared_ptr<int>& other) {
    auto ptr = std::make_shared<int>(42);
    ptr.swap(other);
    *ptr;  // ERROR: can't prove _nonnull after swap
}

// =============================================================================
// SECTION 10: Return value from function, unguarded
// =============================================================================

std::shared_ptr<int> maybe_create(bool b) {
    if (b) return std::make_shared<int>(42);
    return nullptr;
}

// Variant 10.1: unguarded access to function return
void test_return_unguarded() {
    auto ptr = maybe_create(false);
    *ptr;  // ERROR: might be null
}

// Variant 10.2: chained call unguarded
void test_chained_unguarded() {
    *maybe_create(true);  // ERROR: can't prove non-null at compile time
}

// =============================================================================
// SECTION 11: weak_ptr lock() without guard
// =============================================================================

// Variant 11.1: lock() result used without guard
void test_weak_lock_no_guard() {
    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;
    shared.reset();  // weak expires
    auto locked = weak.lock();
    *locked;  // ERROR: lock() might return null
}

// Variant 11.2: lock() on expired weak_ptr
void test_weak_expired_access() {
    std::weak_ptr<int> weak;
    {
        auto shared = std::make_shared<int>(42);
        weak = shared;
    }  // shared goes out of scope
    auto locked = weak.lock();
    *locked;  // ERROR: definitely null
}

// =============================================================================
// SECTION 12: use_count() == 0 branch access
// =============================================================================

// Variant 12.1: access when use_count is 0
void test_use_count_zero() {
    std::shared_ptr<int> ptr;
    if (ptr.use_count() == 0) {
        *ptr;  // ERROR: use_count == 0 means null
    }
}

// =============================================================================
// SECTION 13: Multiple state changes
// =============================================================================

// Variant 13.1: access, reset, access
void test_access_reset_access() {
    auto ptr = std::make_shared<int>(42);
    *ptr = 100;  // OK
    ptr.reset();
    *ptr;  // ERROR: second access after reset
}

// Variant 13.2: multiple resets
void test_multiple_resets() {
    auto ptr = std::make_shared<int>(42);
    ptr.reset();
    ptr = std::make_shared<int>(43);  // OK, re-assigned
    ptr.reset();
    *ptr;  // ERROR: null after second reset
}

// =============================================================================
// SECTION 14: Comparison doesn't prove nonnull in else
// =============================================================================

// Variant 14.1: comparison false branch
void test_comparison_else() {
    std::shared_ptr<int> ptr;
    if (ptr == nullptr) {
        *ptr;  // ERROR: ptr is null here
    }
}

// Variant 14.2: post-comparison access
void test_post_comparison() {
    auto ptr = std::make_shared<int>(42);
    bool is_null = (ptr == nullptr);
    ptr.reset();
    if (!is_null) {
        *ptr;  // ERROR: comparison was before reset
    }
}

// =============================================================================
// SECTION 15: aliasing constructor from null
// =============================================================================

// Variant 15.1: aliasing from null shared_ptr
void test_aliasing_null() {
    std::shared_ptr<int> null_ptr;
    int dummy = 42;
    std::shared_ptr<int> alias(null_ptr, &dummy);
    // alias has a non-null stored pointer but null control block
    // This is a weird edge case, but get() returns non-null here
    // However the original is still null
    *null_ptr;  // ERROR: null_ptr is still null
}

int main() {
    return 0;
}
