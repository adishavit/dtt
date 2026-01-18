// 845_mutex_xfail.cpp
// Tests: Unsafe std::mutex/lock usage that DTT SHOULD catch

#include <mutex>
#include <shared_mutex>

// =============================================================================
// SECTION 1: Unlock without lock
// =============================================================================

// Variant 1.1: unlock on never-locked mutex
void test_unlock_not_locked() {
    std::mutex m;
    m.unlock();  // ERROR: mutex was never locked
}

// Variant 1.2: double unlock
void test_double_unlock() {
    std::mutex m;
    m.lock();
    m.unlock();
    m.unlock();  // ERROR: already unlocked
}

// =============================================================================
// SECTION 2: Lock not released
// =============================================================================

// Variant 2.1: lock without unlock (non-RAII)
void test_no_unlock() {
    std::mutex m;
    m.lock();
    // ERROR: function returns without unlocking
    // (This is a deadlock/resource leak)
}

// =============================================================================
// SECTION 3: unique_lock misuse
// =============================================================================

// Variant 3.1: unlock on defer_lock without locking first
void test_defer_unlock_not_locked() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m, std::defer_lock);
    lock.unlock();  // ERROR: never locked
}

// Variant 3.2: double lock on unique_lock
void test_unique_lock_double_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m);
    lock.lock();  // ERROR: already locked
}

// Variant 3.3: lock on already-locked unique_lock
void test_unique_lock_relock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m);
    lock.lock();  // ERROR: already owns lock
}

// =============================================================================
// SECTION 4: adopt_lock on unlocked mutex
// =============================================================================

// Variant 4.1: adopt_lock when mutex not locked
void test_adopt_not_locked() {
    std::mutex m;
    // Not locked!
    std::unique_lock<std::mutex> lock(m, std::adopt_lock);
    // ERROR: adopt assumes mutex is already locked
}  // Will try to unlock an unlocked mutex

// =============================================================================
// SECTION 5: Wrong unlock in branch
// =============================================================================

// Variant 5.1: unlock in failure branch
void test_wrong_branch_unlock() {
    std::mutex m;
    if (m.try_lock()) {
        // OK here
    } else {
        m.unlock();  // ERROR: didn't acquire lock
    }
}

// =============================================================================
// SECTION 6: Recursive lock on non-recursive mutex
// =============================================================================

// Variant 6.1: double lock on regular mutex
void test_double_lock() {
    std::mutex m;
    m.lock();
    m.lock();  // ERROR: deadlock (or UB) - not recursive_mutex
}

// Variant 6.2: nested lock_guard on same mutex
void test_nested_lock_guard() {
    std::mutex m;
    std::lock_guard<std::mutex> lock1(m);
    std::lock_guard<std::mutex> lock2(m);  // ERROR: deadlock
}

// =============================================================================
// SECTION 7: Access after release
// =============================================================================

// Variant 7.1: use lock after release
void test_use_after_release() {
    std::mutex m;
    std::unique_lock<std::mutex> lock(m);
    lock.release();  // Releases ownership
    // Now lock doesn't own the mutex, m is still locked!
    lock.unlock();  // ERROR: lock doesn't own mutex
}

// =============================================================================
// SECTION 8: Wrong mutex unlocked
// =============================================================================

// Variant 8.1: lock m1, unlock m2
void test_wrong_mutex() {
    std::mutex m1, m2;
    m1.lock();
    m2.unlock();  // ERROR: m2 was never locked
    m1.unlock();
}

// =============================================================================
// SECTION 9: shared_lock misuse
// =============================================================================

// Variant 9.1: exclusive operations with shared_lock
void test_shared_lock_write() {
    std::shared_mutex m;
    std::shared_lock<std::shared_mutex> lock(m);
    // shared_lock is for reading only
    // Writing here would be a data race (not directly detectable by DTT)
}

// Variant 9.2: unlock shared_lock without lock
void test_shared_unlock_not_locked() {
    std::shared_mutex m;
    std::shared_lock<std::shared_mutex> lock(m, std::defer_lock);
    lock.unlock();  // ERROR: never locked
}

// =============================================================================
// SECTION 10: Moved-from lock
// =============================================================================

// Variant 10.1: use moved-from unique_lock
void test_moved_from_lock() {
    std::mutex m;
    std::unique_lock<std::mutex> lock1(m);
    std::unique_lock<std::mutex> lock2 = std::move(lock1);
    lock1.unlock();  // ERROR: lock1 is moved-from
}

// =============================================================================
// SECTION 11: timed_mutex misuse
// =============================================================================

// Variant 11.1: unlock when try_lock_for failed
void test_timed_unlock_on_fail() {
    std::timed_mutex m;
    m.lock();  // Some other thread has it
    if (!m.try_lock_for(std::chrono::milliseconds(1))) {
        m.unlock();  // ERROR: didn't acquire lock
    }
}

// =============================================================================
// SECTION 12: Destroy locked mutex
// =============================================================================

// Variant 12.1: mutex destroyed while locked
void test_destroy_locked() {
    std::mutex* m = new std::mutex();
    m->lock();
    delete m;  // ERROR: destroying locked mutex is UB
}

int main() {
    return 0;
}
