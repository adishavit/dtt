// 847_future_xfail.cpp
// Tests: Unsafe std::future/promise usage that DTT SHOULD catch

#include <future>
#include <thread>

// =============================================================================
// SECTION 1: get() without valid() check
// =============================================================================

// Variant 1.1: get() on default-constructed future
void test_get_invalid() {
    std::future<int> f;  // Not valid
    int val = f.get();  // ERROR: future not valid
    (void)val;
}

// Variant 1.2: get() on parameter future
void test_get_param(std::future<int>& f) {
    int val = f.get();  // ERROR: can't prove f is valid
    (void)val;
}

// =============================================================================
// SECTION 2: Double get() on future
// =============================================================================

// Variant 2.1: get() called twice
void test_double_get() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_value(42);
    f.get();     // OK: first get
    f.get();     // ERROR: future no longer valid after first get
}

// =============================================================================
// SECTION 3: get() in wrong branch
// =============================================================================

// Variant 3.1: get in else of valid()
void test_wrong_branch() {
    std::future<int> f;
    if (f.valid()) {
        // OK here
    } else {
        int val = f.get();  // ERROR: not valid in else branch
        (void)val;
    }
}

// Variant 3.2: get when wait_for returns timeout
void test_timeout_branch() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    // Don't set value
    if (f.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
        int val = f.get();  // ERROR: not ready, might block forever
        (void)val;
    }
}

// =============================================================================
// SECTION 4: get() on moved-from future
// =============================================================================

// Variant 4.1: get after move
void test_get_moved_from() {
    std::promise<int> p;
    std::future<int> f1 = p.get_future();
    std::future<int> f2 = std::move(f1);
    p.set_value(42);
    int val = f1.get();  // ERROR: f1 is moved-from
    (void)val;
}

// =============================================================================
// SECTION 5: get() after share()
// =============================================================================

// Variant 5.1: get on original after share
void test_get_after_share() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    std::shared_future<int> sf = f.share();
    p.set_value(42);
    int val = f.get();  // ERROR: f is invalid after share()
    (void)val;
}

// =============================================================================
// SECTION 6: get_future() called twice
// =============================================================================

// Variant 6.1: double get_future on promise
void test_double_get_future() {
    std::promise<int> p;
    std::future<int> f1 = p.get_future();
    std::future<int> f2 = p.get_future();  // ERROR: future already retrieved
    (void)f1; (void)f2;
}

// =============================================================================
// SECTION 7: set_value() called twice
// =============================================================================

// Variant 7.1: double set_value on promise
void test_double_set_value() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_value(1);
    p.set_value(2);  // ERROR: promise already satisfied
    (void)f;
}

// Variant 7.2: set_value after set_exception
void test_set_after_exception() {
    std::promise<int> p;
    p.set_exception(std::make_exception_ptr(std::runtime_error("err")));
    p.set_value(42);  // ERROR: promise already satisfied
}

// =============================================================================
// SECTION 8: Use promise after move
// =============================================================================

// Variant 8.1: set_value on moved-from promise
void test_moved_from_promise() {
    std::promise<int> p1;
    std::promise<int> p2 = std::move(p1);
    p1.set_value(42);  // ERROR: p1 is moved-from
}

// =============================================================================
// SECTION 9: wait on invalid future
// =============================================================================

// Variant 9.1: wait() on invalid future
void test_wait_invalid() {
    std::future<int> f;
    f.wait();  // ERROR: future not valid
}

// Variant 9.2: wait_for on invalid future
void test_wait_for_invalid() {
    std::future<int> f;
    f.wait_for(std::chrono::seconds(1));  // ERROR: future not valid
}

// =============================================================================
// SECTION 10: Broken promise (promise destroyed without setting value)
// =============================================================================

// Variant 10.1: promise destroyed without set_value
void test_broken_promise() {
    std::future<int> f;
    {
        std::promise<int> p;
        f = p.get_future();
        // p destroyed without set_value - broken_promise exception
    }
    try {
        f.get();  // Will throw broken_promise (but that's allowed)
    } catch (...) {}
}

// =============================================================================
// SECTION 11: packaged_task misuse
// =============================================================================

// Variant 11.1: get_future twice on packaged_task
void test_packaged_task_double_future() {
    std::packaged_task<int()> task([]() { return 42; });
    auto f1 = task.get_future();
    auto f2 = task.get_future();  // ERROR: future already retrieved
    (void)f1; (void)f2;
}

// Variant 11.2: invoke task after move
void test_packaged_task_moved() {
    std::packaged_task<int()> task1([]() { return 42; });
    std::packaged_task<int()> task2 = std::move(task1);
    task1();  // ERROR: task1 is moved-from
}

// =============================================================================
// SECTION 12: shared_future - get after move
// =============================================================================

// Variant 12.1: get on moved-from shared_future
void test_shared_future_moved() {
    std::promise<int> p;
    std::shared_future<int> sf1 = p.get_future().share();
    std::shared_future<int> sf2 = std::move(sf1);
    p.set_value(42);
    int val = sf1.get();  // ERROR: sf1 is moved-from
    (void)val;
}

int main() {
    return 0;
}
