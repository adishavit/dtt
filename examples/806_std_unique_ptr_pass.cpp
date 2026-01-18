// 806_std_unique_ptr_pass.cpp
// Tests: Safe std::unique_ptr usage that should compile without DTT errors

#include <memory>

// =============================================================================
// SECTION 1: Guard with if (ptr) before access
// =============================================================================

// Variant 1.1: if (ptr) guard then dereference
void test_if_guard_deref() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr) {
        *ptr = 100;  // OK: guarded by if (ptr)
    }
}

// Variant 1.2: if (ptr) guard then arrow
void test_if_guard_arrow() {
    struct S { int x; };
    std::unique_ptr<S> ptr = std::make_unique<S>();
    if (ptr) {
        ptr->x = 42;  // OK: guarded
    }
}

// Variant 1.3: if (ptr != nullptr) guard
void test_explicit_nullptr_guard() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 1.4: early return guard
void test_early_return_guard() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (!ptr) return;
    *ptr = 100;  // OK: guarded by early return
}

// =============================================================================
// SECTION 2: Immediate access after make_unique
// =============================================================================

// Variant 2.1: access right after make_unique
void test_make_unique_immediate() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    *ptr = 100;  // OK: make_unique always returns non-null (or throws)
}

// Variant 2.2: arrow access after make_unique
void test_make_unique_arrow() {
    struct S { int x; void set(int v) { x = v; } };
    auto ptr = std::make_unique<S>();
    ptr->set(42);  // OK: make_unique guarantees non-null
}

// Variant 2.3: chained make_unique
void test_make_unique_chained() {
    *std::make_unique<int>(42);  // OK: temporary is non-null
}

// =============================================================================
// SECTION 3: Access after reset(new ...)
// =============================================================================

// Variant 3.1: reset with new allocation
void test_reset_new() {
    std::unique_ptr<int> ptr;
    ptr.reset(new int(42));
    *ptr = 100;  // OK: reset(non-null) sets _nonnull = true
}

// Variant 3.2: reset with make_unique release
void test_reset_make_unique() {
    std::unique_ptr<int> ptr;
    ptr.reset(std::make_unique<int>(42).release());
    *ptr;  // OK: non-null
}

// =============================================================================
// SECTION 4: Guard with get() != nullptr
// =============================================================================

// Variant 4.1: get() guard
void test_get_guard() {
    std::unique_ptr<int> ptr;
    if (ptr.get() != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 4.2: get() with early return
void test_get_early_return() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr.get() == nullptr) return;
    *ptr;  // OK: guarded
}

// =============================================================================
// SECTION 5: operator bool() guard
// =============================================================================

// Variant 5.1: bool conversion in if
void test_bool_conversion() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (static_cast<bool>(ptr)) {
        *ptr;  // OK: guarded
    }
}

// Variant 5.2: negated bool with early return
void test_negated_bool() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (!static_cast<bool>(ptr)) return;
    *ptr;  // OK: guarded
}

// =============================================================================
// SECTION 6: Access through get()
// =============================================================================

// Variant 6.1: get() access with guard on raw pointer
void test_get_access_guarded() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr) {
        int* raw = ptr.get();
        if (raw) {
            *raw = 100;  // OK: raw is guarded
        }
    }
}

// Variant 6.2: get() immediate after make_unique with guard
void test_get_immediate() {
    auto ptr = std::make_unique<int>(42);
    int* raw = ptr.get();
    if (raw) {
        *raw = 100;  // OK: raw is guarded
    }
}

// =============================================================================
// SECTION 7: Reassignment tracking
// =============================================================================

// Variant 7.1: reassign with make_unique
void test_reassign_make_unique() {
    std::unique_ptr<int> ptr;
    ptr = std::make_unique<int>(42);
    *ptr;  // OK: reassigned to non-null
}

// Variant 7.2: reassign in else branch
void test_reassign_else(bool cond) {
    std::unique_ptr<int> ptr = std::make_unique<int>(1);
    if (cond) {
        *ptr;  // OK: still has initial value
    } else {
        ptr = std::make_unique<int>(2);
        *ptr;  // OK: reassigned
    }
}

// =============================================================================
// SECTION 8: Array unique_ptr
// =============================================================================

// Variant 8.1: array access with guard
void test_array_guarded() {
    std::unique_ptr<int[]> arr = std::make_unique<int[]>(10);
    if (arr) {
        arr[0] = 42;  // OK: guarded
    }
}

// Variant 8.2: array access immediate
void test_array_immediate() {
    auto arr = std::make_unique<int[]>(10);
    arr[5] = 100;  // OK: make_unique guarantees non-null
}

// =============================================================================
// SECTION 9: Custom deleter
// =============================================================================

// Variant 9.1: custom deleter with guard
void test_custom_deleter() {
    auto deleter = [](int* p) { delete p; };
    std::unique_ptr<int, decltype(deleter)> ptr(new int(42), deleter);
    if (ptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 9.2: custom deleter immediate after new
void test_custom_deleter_immediate() {
    auto deleter = [](int* p) { delete p; };
    std::unique_ptr<int, decltype(deleter)> ptr(new int(42), deleter);
    *ptr;  // OK: initialized with non-null
}

// =============================================================================
// SECTION 10: Move and access
// =============================================================================

// Variant 10.1: move then access destination
void test_move_access_dest() {
    auto src = std::make_unique<int>(42);
    std::unique_ptr<int> dest = std::move(src);
    *dest;  // OK: dest now owns the non-null pointer
}

// Variant 10.2: access before move
void test_access_before_move() {
    auto ptr = std::make_unique<int>(42);
    *ptr = 100;  // OK: before move
    auto other = std::move(ptr);
}

// =============================================================================
// SECTION 11: Comparison guards
// =============================================================================

// Variant 11.1: comparison with nullptr
void test_compare_nullptr() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    if (ptr != nullptr) {
        *ptr;  // OK: guarded
    }
}

// Variant 11.2: comparison with another unique_ptr (non-null branch)
void test_compare_unique_ptr() {
    auto ptr1 = std::make_unique<int>(42);
    auto ptr2 = std::make_unique<int>(43);
    if (ptr1 && ptr2) {
        *ptr1 + *ptr2;  // OK: both guarded
    }
}

// =============================================================================
// SECTION 12: Ternary with guard
// =============================================================================

// Variant 12.1: ternary access
int test_ternary_access() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    return ptr ? *ptr : 0;  // OK: guarded in true branch
}

// Variant 12.2: ternary with get
int* test_ternary_get() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    return ptr ? ptr.get() : nullptr;  // OK
}

// =============================================================================
// SECTION 13: Lambda capture with guard
// =============================================================================

// Variant 13.1: lambda captures and guards
void test_lambda_capture() {
    auto ptr = std::make_unique<int>(42);
    auto f = [&ptr]() {
        if (ptr) {
            *ptr = 100;  // OK: guarded inside lambda
        }
    };
    f();
}

// =============================================================================
// SECTION 14: release() after check
// =============================================================================

// Variant 14.1: release after guard
int* test_release_guarded() {
    auto ptr = std::make_unique<int>(42);
    if (ptr) {
        return ptr.release();  // OK: guarded
    }
    return nullptr;
}

// Variant 14.2: release immediate
int* test_release_immediate() {
    return std::make_unique<int>(42).release();  // OK: make_unique is non-null
}

// =============================================================================
// SECTION 15: swap with known states
// =============================================================================

// Variant 15.1: swap two non-null
void test_swap_both_nonnull() {
    auto p1 = std::make_unique<int>(1);
    auto p2 = std::make_unique<int>(2);
    p1.swap(p2);
    *p1;  // OK: both were non-null
    *p2;  // OK
}

// Variant 15.2: swap then guard
void test_swap_then_guard() {
    auto p1 = std::make_unique<int>(1);
    std::unique_ptr<int> p2;
    p1.swap(p2);
    if (p1) {
        *p1;  // OK: guarded
    }
}

int main() {
    return 0;
}
