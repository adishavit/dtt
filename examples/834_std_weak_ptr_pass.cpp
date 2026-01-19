// 834_std_weak_ptr_pass.cpp
// Tests: Safe std::weak_ptr usage that should compile without DTT errors

#include <memory>

// =============================================================================
// SECTION 1: Guard with lock() before access
// =============================================================================

// Variant 1.1: if (auto sp = wp.lock()) guard
void test_lock_guard() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (auto locked = wp.lock()) {
        *locked;  // OK: lock succeeded
    }
}

// Variant 1.2: lock and store, then check
void test_lock_store_check() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    std::shared_ptr<int> locked = wp.lock();
    if (locked) {
        *locked;  // OK: checked non-null
    }
}

// Variant 1.3: lock with explicit nullptr check
void test_lock_nullptr_check() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    auto locked = wp.lock();
    if (locked != nullptr) {
        *locked;  // OK: explicit null check
    }
}

// =============================================================================
// SECTION 2: Guard with expired() before lock
// =============================================================================

// Variant 2.1: if (!expired()) then lock
void test_expired_guard() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (!wp.expired()) {
        if (auto locked = wp.lock()) {
            *locked;  // OK: double guarded
        }
    }
}

// Variant 2.2: early return on expired
void test_early_return_expired() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (wp.expired()) return;
    if (auto locked = wp.lock()) {
        *locked;  // OK: guarded
    }
}

// =============================================================================
// SECTION 3: Access via locked shared_ptr
// =============================================================================

// Variant 3.1: lock returns valid shared_ptr
void test_access_via_lock() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (auto locked = wp.lock()) {
        locked.get();  // OK: get() on valid shared_ptr
        *locked;       // OK: dereference valid shared_ptr
    }
}

// Variant 3.2: use locked pointer multiple times
void test_multiple_access() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (auto locked = wp.lock()) {
        int a = *locked;  // OK
        int b = *locked;  // OK: same locked pointer
        (void)a; (void)b;
    }
}

// =============================================================================
// SECTION 4: Copy weak_ptr and lock
// =============================================================================

// Variant 4.1: copy weak_ptr
void test_copy_weak_ptr() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp1 = sp;
    std::weak_ptr<int> wp2 = wp1;  // Copy
    if (auto locked = wp2.lock()) {
        *locked;  // OK: copied weak_ptr can lock
    }
}

// =============================================================================
// SECTION 5: Reset and recheck
// =============================================================================

// Variant 5.1: reset then check expired
void test_reset_check() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    wp.reset();
    if (wp.expired()) {
        // OK: correctly identified expired
    }
}

// Variant 5.2: reassign to new shared_ptr
void test_reassign() {
    auto sp1 = std::make_shared<int>(42);
    auto sp2 = std::make_shared<int>(100);
    std::weak_ptr<int> wp = sp1;
    wp = sp2;  // Reassign
    if (auto locked = wp.lock()) {
        *locked;  // OK: locked from sp2
    }
}

// =============================================================================
// SECTION 6: use_count check
// =============================================================================

// Variant 6.1: use_count > 0 implies lockable
void test_use_count() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (wp.use_count() > 0) {
        if (auto locked = wp.lock()) {
            *locked;  // OK: use_count > 0
        }
    }
}

// =============================================================================
// SECTION 7: Owner-based comparison
// =============================================================================

// Variant 7.1: owner_before comparison
void test_owner_before() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp1 = sp;
    std::weak_ptr<int> wp2 = sp;
    if (!wp1.owner_before(wp2) && !wp2.owner_before(wp1)) {
        // Same owner - both should lock or both fail
        if (auto l1 = wp1.lock()) {
            *l1;  // OK
        }
    }
}

// =============================================================================
// SECTION 8: swap and recheck
// =============================================================================

// Variant 8.1: swap then lock
void test_swap() {
    auto sp1 = std::make_shared<int>(42);
    auto sp2 = std::make_shared<int>(100);
    std::weak_ptr<int> wp1 = sp1;
    std::weak_ptr<int> wp2 = sp2;
    wp1.swap(wp2);
    if (auto locked = wp1.lock()) {
        *locked;  // OK: wp1 now points to sp2
    }
}

// =============================================================================
// SECTION 9: enable_shared_from_this pattern
// =============================================================================

struct Widget : std::enable_shared_from_this<Widget> {
    int value = 42;

    // Variant 9.1: weak_from_this with lock
    void safe_callback() {
        std::weak_ptr<Widget> self = weak_from_this();
        if (auto locked = self.lock()) {
            locked->value;  // OK: locked
        }
    }
};

void test_enable_shared_from_this() {
    auto w = std::make_shared<Widget>();
    w->safe_callback();
}

int main() {
    return 0;
}
