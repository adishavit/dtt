// 846_future_pass.cpp
// Tests: Safe std::future/promise usage that should compile without DTT errors

#include <future>
#include <thread>

// =============================================================================
// SECTION 1: Guard with valid() before get()
// =============================================================================

// Variant 1.1: if (valid()) guard then get()
void test_valid_guard() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_value(42);
    if (f.valid()) {
        int val = f.get();  // OK: guarded
        (void)val;
    }
}

// Variant 1.2: early return on !valid()
void test_early_return() {
    std::future<int> f;  // Default constructed, not valid
    if (!f.valid()) return;
    int val = f.get();  // OK: guarded
    (void)val;
}

// =============================================================================
// SECTION 2: Wait before get (ensures ready)
// =============================================================================

// Variant 2.1: wait() then get()
void test_wait_then_get() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    std::thread t([&p]() { p.set_value(42); });
    f.wait();  // Blocks until ready
    int val = f.get();  // OK: wait completed
    (void)val;
    t.join();
}

// Variant 2.2: wait_for returns ready
void test_wait_for_ready() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_value(42);
    if (f.wait_for(std::chrono::seconds(1)) == std::future_status::ready) {
        int val = f.get();  // OK: confirmed ready
        (void)val;
    }
}

// Variant 2.3: wait_until returns ready
void test_wait_until_ready() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_value(42);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (f.wait_until(deadline) == std::future_status::ready) {
        int val = f.get();  // OK: confirmed ready
        (void)val;
    }
}

// =============================================================================
// SECTION 3: async returns valid future
// =============================================================================

// Variant 3.1: async with get
void test_async_get() {
    auto f = std::async(std::launch::async, []() { return 42; });
    int val = f.get();  // OK: async returns valid future
    (void)val;
}

// Variant 3.2: async with deferred
void test_async_deferred() {
    auto f = std::async(std::launch::deferred, []() { return 42; });
    if (f.valid()) {
        int val = f.get();  // OK: valid and get triggers execution
        (void)val;
    }
}

// =============================================================================
// SECTION 4: shared_future (can call get multiple times)
// =============================================================================

// Variant 4.1: shared_future multiple gets
void test_shared_future() {
    std::promise<int> p;
    std::shared_future<int> sf = p.get_future().share();
    p.set_value(42);
    if (sf.valid()) {
        int val1 = sf.get();  // OK: first get
        int val2 = sf.get();  // OK: shared_future allows multiple gets
        (void)val1; (void)val2;
    }
}

// Variant 4.2: copy shared_future
void test_shared_future_copy() {
    std::promise<int> p;
    std::shared_future<int> sf1 = p.get_future().share();
    std::shared_future<int> sf2 = sf1;  // Copy
    p.set_value(42);
    if (sf2.valid()) {
        int val = sf2.get();  // OK: copied shared_future
        (void)val;
    }
}

// =============================================================================
// SECTION 5: promise set_value before get_future
// =============================================================================

// Variant 5.1: set_value then get_future
void test_set_before_get_future() {
    std::promise<int> p;
    p.set_value(42);
    std::future<int> f = p.get_future();
    if (f.valid()) {
        int val = f.get();  // OK: value already set
        (void)val;
    }
}

// =============================================================================
// SECTION 6: packaged_task
// =============================================================================

// Variant 6.1: packaged_task with get_future
void test_packaged_task() {
    std::packaged_task<int()> task([]() { return 42; });
    std::future<int> f = task.get_future();
    task();  // Execute task
    if (f.valid()) {
        int val = f.get();  // OK: task completed
        (void)val;
    }
}

// Variant 6.2: packaged_task in thread
void test_packaged_task_thread() {
    std::packaged_task<int()> task([]() { return 42; });
    std::future<int> f = task.get_future();
    std::thread t(std::move(task));
    f.wait();
    int val = f.get();  // OK: waited for completion
    (void)val;
    t.join();
}

// =============================================================================
// SECTION 7: Exception handling
// =============================================================================

// Variant 7.1: set_exception
void test_set_exception() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    p.set_exception(std::make_exception_ptr(std::runtime_error("error")));
    try {
        f.get();  // OK: will throw, but that's expected
    } catch (const std::runtime_error&) {
        // OK: handled
    }
}

// =============================================================================
// SECTION 8: future<void>
// =============================================================================

// Variant 8.1: void promise/future
void test_void_future() {
    std::promise<void> p;
    std::future<void> f = p.get_future();
    p.set_value();
    if (f.valid()) {
        f.get();  // OK: void get
    }
}

// =============================================================================
// SECTION 9: Check status without blocking
// =============================================================================

// Variant 9.1: wait_for with zero timeout
void test_zero_wait() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    auto status = f.wait_for(std::chrono::seconds(0));
    if (status == std::future_status::ready) {
        int val = f.get();  // OK: confirmed ready
        (void)val;
    }
    // If not ready, don't call get
}

// =============================================================================
// SECTION 10: valid() after share
// =============================================================================

// Variant 10.1: check valid after share
void test_valid_after_share() {
    std::promise<int> p;
    std::future<int> f = p.get_future();
    std::shared_future<int> sf = f.share();
    // f is now invalid, sf is valid
    if (sf.valid()) {
        p.set_value(42);
        int val = sf.get();  // OK: sf is valid
        (void)val;
    }
}

int main() {
    return 0;
}
