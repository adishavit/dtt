// 844_mutex_pass.cpp
// Tests: Safe std::mutex/lock usage that should compile without DTT errors

#include <mutex>
#include <shared_mutex>

// =============================================================================
// SECTION 1: RAII locks (always safe)
// =============================================================================

// Variant 1.1: lock_guard
void test_lock_guard() {
    std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    // OK: mutex locked for scope duration
}

// Variant 1.2: unique_lock
void test_unique_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m);
    // OK: mutex locked for scope duration
}

// Variant 1.3: scoped_lock (C++17)
void test_scoped_lock() {
    std::mutex m1, m2;
    std::scoped_lock lock(m1, m2);
    // OK: both mutexes locked, deadlock-free
}

// =============================================================================
// SECTION 2: Manual lock/unlock in same scope
// =============================================================================

// Variant 2.1: lock then unlock
void test_manual_lock_unlock() {
    std::mutex m;
    m.lock();
    // ... critical section ...
    m.unlock();  // OK: balanced
}

// Variant 2.2: try_lock with unlock on success
void test_try_lock() {
    std::mutex m;
    if (m.try_lock()) {
        // ... critical section ...
        m.unlock();  // OK: only unlock if locked
    }
}

// =============================================================================
// SECTION 3: unique_lock with defer_lock
// =============================================================================

// Variant 3.1: defer_lock then lock
void test_defer_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m, std::defer_lock);
    // ... do something ...
    lock.lock();  // OK: explicitly lock later
    // ... critical section ...
}  // unlock on destruction

// Variant 3.2: defer_lock with try_lock
void test_defer_try_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m, std::defer_lock);
    if (lock.try_lock()) {
        // OK: locked successfully
    }
}

// =============================================================================
// SECTION 4: unique_lock with adopt_lock
// =============================================================================

// Variant 4.1: adopt already-locked mutex
void test_adopt_lock() {
    std::mutex m;
    m.lock();  // Manually lock
    std::unique_lock<std::mutex> lock(m, std::adopt_lock);
    // OK: lock takes ownership, will unlock
}

// =============================================================================
// SECTION 5: Check owns_lock before unlock
// =============================================================================

// Variant 5.1: owns_lock guard
void test_owns_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m, std::defer_lock);
    // ... some code that might lock ...
    if (lock.owns_lock()) {
        lock.unlock();  // OK: guarded
    }
}

// Variant 5.2: owns_lock before access
void test_owns_lock_access() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m);
    if (lock.owns_lock()) {
        // OK: mutex is definitely locked
    }
}

// =============================================================================
// SECTION 6: shared_mutex with shared_lock
// =============================================================================

// Variant 6.1: shared_lock for reading
void test_shared_lock() {
    std::shared_mutex m;
    std::shared_lock<std::shared_mutex> lock(m);
    // OK: shared (read) lock acquired
}

// Variant 6.2: unique_lock for writing
void test_unique_lock_shared_mutex() {
    std::shared_mutex m;
    std::unique_lock<std::shared_mutex> lock(m);
    // OK: exclusive (write) lock acquired
}

// =============================================================================
// SECTION 7: recursive_mutex
// =============================================================================

// Variant 7.1: recursive locking
void test_recursive_mutex() {
    std::recursive_mutex m;
    m.lock();
    m.lock();  // OK: recursive mutex allows this
    m.unlock();
    m.unlock();  // OK: balanced
}

// Variant 7.2: recursive with lock_guard
void recursive_helper(std::recursive_mutex& m) {
    std::lock_guard<std::recursive_mutex> lock(m);
    // OK: recursive mutex
}

void test_recursive_call() {
    std::recursive_mutex m;
    std::lock_guard<std::recursive_mutex> lock(m);
    recursive_helper(m);  // OK: recursive mutex handles this
}

// =============================================================================
// SECTION 8: timed_mutex with try_lock_for
// =============================================================================

// Variant 8.1: try_lock_for with check
void test_timed_mutex() {
    std::timed_mutex m;
    if (m.try_lock_for(std::chrono::milliseconds(100))) {
        // OK: locked successfully
        m.unlock();
    }
}

// =============================================================================
// SECTION 9: std::lock for multiple mutexes
// =============================================================================

// Variant 9.1: std::lock with adopt_lock
void test_std_lock() {
    std::mutex m1, m2;
    std::lock(m1, m2);  // Lock both, deadlock-free
    std::lock_guard<std::mutex> lock1(m1, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(m2, std::adopt_lock);
    // OK: both locked and will be unlocked
}

// =============================================================================
// SECTION 10: call_once
// =============================================================================

// Variant 10.1: call_once with flag
void test_call_once() {
    std::once_flag flag;
    std::call_once(flag, []() {
        // OK: called exactly once
    });
}

int main() {
    return 0;
}
