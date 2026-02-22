// 835_std_weak_ptr_fail.cpp
// Tests: Unsafe std::weak_ptr usage that DTT SHOULD catch

#include <memory>

// =============================================================================
// SECTION 1: Dereference lock() without checking
// =============================================================================

// Variant 1.1: dereference lock() result without null check
void test_lock_no_check() {
    std::weak_ptr<int> wp;  // Expired
    *wp.lock();  // ERROR: lock() returns nullptr
}

// Variant 1.2: dereference lock() on parameter
void test_lock_param(std::weak_ptr<int>& wp) {
    *wp.lock();  // ERROR: can't prove lock() succeeds
}

// Variant 1.3: store lock result but don't check
void test_lock_store_no_check() {
    std::weak_ptr<int> wp;
    auto sp = wp.lock();
    *sp;  // ERROR: sp might be nullptr
}

// =============================================================================
// SECTION 2: Access after shared_ptr destroyed
// =============================================================================

// Variant 2.1: shared_ptr goes out of scope
void test_shared_destroyed() {
    std::weak_ptr<int> wp;
    {
        auto sp = std::make_shared<int>(42);
        wp = sp;
    }  // sp destroyed here
    *wp.lock();  // ERROR: lock() returns nullptr
}

// Variant 2.2: shared_ptr reset
void test_shared_reset() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    sp.reset();
    *wp.lock();  // ERROR: lock() returns nullptr
}

// =============================================================================
// SECTION 3: Access in wrong branch
// =============================================================================

// Variant 3.1: access in else of lock check
void test_wrong_branch() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (auto locked = wp.lock()) {
        // OK here
    } else {
        *wp.lock();  // ERROR: already failed to lock
    }
}

// Variant 3.2: access when expired() is true
void test_expired_access() {
    std::weak_ptr<int> wp;
    if (wp.expired()) {
        *wp.lock();  // ERROR: known to be expired
    }
}

// =============================================================================
// SECTION 4: Stale lock result
// =============================================================================

// Variant 4.1: lock result invalidated by reset
void test_stale_lock() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    auto locked = wp.lock();
    sp.reset();  // Now locked is only reference
    // But if we make another lock attempt:
    *wp.lock();  // ERROR: new lock() returns nullptr
}

// =============================================================================
// SECTION 5: Confusing expired() semantics
// =============================================================================

// Variant 5.1: expired() without follow-up lock check
void test_expired_no_lock() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    if (!wp.expired()) {
        // Expired might become true before lock!
        // This is a TOCTOU issue
        *wp.lock();  // ERROR: lock might still fail
    }
}

// =============================================================================
// SECTION 6: Access moved-from weak_ptr
// =============================================================================

// Variant 6.1: lock moved-from weak_ptr
void test_moved_from() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp1 = sp;
    std::weak_ptr<int> wp2 = std::move(wp1);
    *wp1.lock();  // ERROR: moved-from state
}

// =============================================================================
// SECTION 7: Default constructed weak_ptr
// =============================================================================

// Variant 7.1: lock() on default constructed
void test_default_constructed() {
    std::weak_ptr<int> wp;
    *wp.lock();  // ERROR: default constructed, expired
}

// =============================================================================
// SECTION 8: Access after swap
// =============================================================================

// Variant 8.1: stale assumption after swap
void test_after_swap() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp1 = sp;
    std::weak_ptr<int> wp2;  // Expired
    wp1.swap(wp2);
    *wp1.lock();  // ERROR: wp1 now points to nothing
}

// =============================================================================
// SECTION 9: Wrong weak_ptr checked
// =============================================================================

// Variant 9.1: check wp1, access wp2
void test_wrong_wp_checked() {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp1 = sp;
    std::weak_ptr<int> wp2;  // Expired
    if (auto locked = wp1.lock()) {
        *wp2.lock();  // ERROR: checked wp1, not wp2
    }
}

// =============================================================================
// SECTION 10: Use get() on lock result without check
// =============================================================================

// Variant 10.1: get() without null check
void test_get_no_check() {
    std::weak_ptr<int> wp;
    auto sp = wp.lock();
    int* p = sp.get();
    *p;  // ERROR: p is nullptr
}

int main() {
    return 0;
}
