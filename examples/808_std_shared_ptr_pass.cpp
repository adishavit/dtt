// 808_std_shared_ptr_pass.cpp
// Tests: Safe std::shared_ptr usage that should compile without DTT errors

#include <memory>

// =============================================================================
// SECTION 1: Guard with if (ptr) before access
// =============================================================================

// Variant 1.1: if (ptr) guard then dereference
void test_if_guard_deref() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr) {
        *ptr = 100;  // OK: guarded by if (ptr)
    }
}

// Variant 1.2: if (ptr) guard then arrow
void test_if_guard_arrow() {
    struct S { int x; };
    std::shared_ptr<S> ptr = std::make_shared<S>();
    if (ptr) {
        ptr->x = 42;  // OK: guarded
    }
}

// Variant 1.3: if (ptr != nullptr) guard
void test_explicit_nullptr_guard() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 1.4: early return guard
void test_early_return_guard() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (!ptr) return;
    *ptr = 100;  // OK: guarded by early return
}

// =============================================================================
// SECTION 2: Immediate access after make_shared
// =============================================================================

// Variant 2.1: access right after make_shared
void test_make_shared_immediate() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    *ptr = 100;  // OK: make_shared always returns non-null (or throws)
}

// Variant 2.2: arrow access after make_shared
void test_make_shared_arrow() {
    struct S { int x; void set(int v) { x = v; } };
    auto ptr = std::make_shared<S>();
    ptr->set(42);  // OK: make_shared guarantees non-null
}

// Variant 2.3: chained make_shared
void test_make_shared_chained() {
    *std::make_shared<int>(42);  // OK: temporary is non-null
}

// =============================================================================
// SECTION 3: Access after reset(new ...)
// =============================================================================

// Variant 3.1: reset with new allocation
void test_reset_new() {
    std::shared_ptr<int> ptr;
    ptr.reset(new int(42));
    *ptr = 100;  // OK: reset(non-null) sets _nonnull = true
}

// Variant 3.2: reset with make_shared
void test_reset_make_shared() {
    std::shared_ptr<int> ptr;
    ptr = std::make_shared<int>(42);
    *ptr;  // OK: non-null
}

// =============================================================================
// SECTION 4: Guard with get() != nullptr
// =============================================================================

// Variant 4.1: get() guard
void test_get_guard() {
    std::shared_ptr<int> ptr;
    if (ptr.get() != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 4.2: get() with early return
void test_get_early_return() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr.get() == nullptr) return;
    *ptr;  // OK: guarded
}

// =============================================================================
// SECTION 5: operator bool() guard
// =============================================================================

// Variant 5.1: bool conversion in if
void test_bool_conversion() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (static_cast<bool>(ptr)) {
        *ptr;  // OK: guarded
    }
}

// Variant 5.2: negated bool with early return
void test_negated_bool() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (!static_cast<bool>(ptr)) return;
    *ptr;  // OK: guarded
}

// =============================================================================
// SECTION 6: Access through get()
// =============================================================================

// Variant 6.1: get() access with guard on raw pointer
void test_get_access_guarded() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr) {
        int* raw = ptr.get();
        if (raw) {
            *raw = 100;  // OK: raw is guarded
        }
    }
}

// Variant 6.2: get() immediate after make_shared with guard
void test_get_immediate() {
    auto ptr = std::make_shared<int>(42);
    int* raw = ptr.get();
    if (raw) {
        *raw = 100;  // OK: raw is guarded
    }
}

// =============================================================================
// SECTION 7: Copy and access (shared_ptr specific)
// =============================================================================

// Variant 7.1: copy from non-null
void test_copy_nonnull() {
    auto ptr1 = std::make_shared<int>(42);
    std::shared_ptr<int> ptr2 = ptr1;  // copy
    *ptr2;  // OK: copied from non-null
}

// Variant 7.2: assignment from non-null
void test_assign_nonnull() {
    auto ptr1 = std::make_shared<int>(42);
    std::shared_ptr<int> ptr2;
    ptr2 = ptr1;
    *ptr2;  // OK: assigned from non-null
}

// Variant 7.3: multiple copies all valid
void test_multiple_copies() {
    auto ptr1 = std::make_shared<int>(42);
    auto ptr2 = ptr1;
    auto ptr3 = ptr2;
    *ptr1 + *ptr2 + *ptr3;  // OK: all share same non-null object
}

// =============================================================================
// SECTION 8: Move and access destination
// =============================================================================

// Variant 8.1: move then access destination
void test_move_access_dest() {
    auto src = std::make_shared<int>(42);
    std::shared_ptr<int> dest = std::move(src);
    *dest;  // OK: dest now owns a non-null pointer
}

// Variant 8.2: access before move
void test_access_before_move() {
    auto ptr = std::make_shared<int>(42);
    *ptr = 100;  // OK: before move
    auto other = std::move(ptr);
}

// =============================================================================
// SECTION 9: use_count() based guards
// =============================================================================

// Variant 9.1: use_count > 0 implies non-null
void test_use_count_guard() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr.use_count() > 0) {
        *ptr;  // OK: use_count > 0 means non-null
    }
}

// Variant 9.2: unique() implies non-null
void test_unique_guard() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr.unique()) {
        *ptr;  // OK: unique() implies non-null
    }
}

// =============================================================================
// SECTION 10: Comparison guards
// =============================================================================

// Variant 10.1: comparison with nullptr
void test_compare_nullptr() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    if (ptr != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 10.2: comparison with another shared_ptr (both non-null)
void test_compare_shared_ptr() {
    auto ptr1 = std::make_shared<int>(42);
    auto ptr2 = std::make_shared<int>(43);
    if (ptr1 && ptr2) {
        *ptr1 + *ptr2;  // OK: both guarded
    }
}

// =============================================================================
// SECTION 11: Ternary with guard
// =============================================================================

// Variant 11.1: ternary access
int test_ternary_access() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    return ptr ? *ptr : 0;  // OK: guarded in true branch
}

// Variant 11.2: ternary with get
int* test_ternary_get() {
    std::shared_ptr<int> ptr = std::make_shared<int>(42);
    return ptr ? ptr.get() : nullptr;  // OK
}

// =============================================================================
// SECTION 12: Lambda capture with guard
// =============================================================================

// Variant 12.1: lambda captures and guards
void test_lambda_capture() {
    auto ptr = std::make_shared<int>(42);
    auto f = [ptr]() {  // copy capture
        if (ptr) {
            *ptr = 100;  // OK: guarded inside lambda
        }
    };
    f();
}

// Variant 12.2: shared_ptr by value in lambda (always copies)
void test_lambda_copy() {
    auto ptr = std::make_shared<int>(42);
    auto f = [ptr]() {
        *ptr;  // OK: lambda has its own copy, known non-null
    };
    f();
}

// =============================================================================
// SECTION 13: swap with known states
// =============================================================================

// Variant 13.1: swap two non-null
void test_swap_both_nonnull() {
    auto p1 = std::make_shared<int>(1);
    auto p2 = std::make_shared<int>(2);
    p1.swap(p2);
    *p1;  // OK: both were non-null
    *p2;  // OK
}

// Variant 13.2: swap then guard
void test_swap_then_guard() {
    auto p1 = std::make_shared<int>(1);
    std::shared_ptr<int> p2;
    p1.swap(p2);
    if (p1) {
        *p1;  // OK: guarded
    }
}

// =============================================================================
// SECTION 14: weak_ptr lock() with guard
// =============================================================================

// Variant 14.1: lock() with guard
void test_weak_lock_guard() {
    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;
    if (auto locked = weak.lock()) {
        *locked;  // OK: lock() succeeded means non-null
    }
}

// Variant 14.2: lock() expired check
void test_weak_expired() {
    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;
    if (!weak.expired()) {
        auto locked = weak.lock();
        if (locked) {
            *locked;  // OK: double guarded
        }
    }
}

// =============================================================================
// SECTION 15: aliasing constructor
// =============================================================================

// Variant 15.1: aliasing constructor preserves non-null
void test_aliasing_constructor() {
    struct S { int x; int y; };
    auto ptr = std::make_shared<S>();
    std::shared_ptr<int> alias(ptr, &ptr->x);
    *alias;  // OK: aliasing from non-null is non-null
}

// =============================================================================
// SECTION 16: owner_before for ordering (doesn't affect null state)
// =============================================================================

// Variant 16.1: owner_before doesn't change null state
void test_owner_before() {
    auto ptr = std::make_shared<int>(42);
    auto ptr2 = std::make_shared<int>(43);
    bool b = ptr.owner_before(ptr2);
    *ptr;  // OK: owner_before doesn't affect null state
    (void)b;
}

int main() {
    return 0;
}
