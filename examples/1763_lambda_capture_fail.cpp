// 1763: Calling a lambda writes what it captures by reference
//
// A call to a lambda left its by-reference captures as they were, so a
// write in the body was never seen by the caller.

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
template <typename F> void apply(F &&f) { f(); }

// Variant 1: the body makes a captured variable negative.
void variant1_called() {
    int n = 5;
    auto l = [&]() { n = -1; };
    l();
    take(n);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 2: an explicit by-reference capture.
void variant2_explicit() {
    int n = 5;
    auto l = [&n]() { n = -1; };
    l();
    take(n);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 3: the lambda handed to another function.
void variant3_passed() {
    int n = 5;
    auto l = [&]() { n = -1; };
    apply(l);
    take(n);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 4: a by-reference init capture writes its object.
void variant4_init_capture_ref() {
    int x = 5;
    auto l = [&r = x]() { r = -1; };
    l();
    take(x);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 5: a lambda stored, then run by another call.
#include <functional>
void variant5_stored() {
    int x = 1;
    int *p = &x;
    std::function<void()> fn = [&]() { p = nullptr; };
    fn();
    *p = 1;  // dtt-error {{null}}
}

// Variant 6: a lambda run by another lambda.
void variant6_nested() {
    int n = 5;
    auto l = [&]() { n = -1; };
    [&]() { l(); }();
    take(n);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 7: an init capture moved from its source.
#include <utility>
#include <vector>
void variant7_moved_into_capture() {
    std::vector<int> v(3);
    auto l = [w = std::move(v)]() { return w.size(); };
    (void)l;
    v[0] = 1;  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 8: a captured structured binding.
#include <tuple>
int variant8_binding_capture(std::tuple<std::vector<int>> &p) {
    auto &[a] = p;
    if (a.size() < 3) return 0;
    auto f = [&a]() { a.clear(); };
    f();
    return a[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 9: a call in a nested lambda may run an escaped lambda.
std::function<void()> stored;
void store(std::function<void()> f) { stored = f; }
void run();
int variant9_nested_call() {
    std::vector<int> v(3);
    store([&]() { v.clear(); });
    auto g = [&]() {
        if (v.size() > 2) {
            run();
            return v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
        }
        return 0;
    };
    return g();
}

// Variant 10: a lambda that frees a captured pointer.
#include <cstdio>
#include <cstdlib>
void variant10_freed_by_lambda() {
    int *p = static_cast<int *>(malloc(4));
    if (!p) return;
    auto f = [&]() { free(p); };
    f();
    *p = 1;  // dtt-error {{null|freed|cannot verify}}
}

// Variant 11: a generic lambda and a member passed on by reference.
void resetExt(std::vector<int> &v);
struct HasVec { std::vector<int> v; };
int variant11_generic_and_member(HasVec &s) {
    std::vector<int> v(3);
    auto f = [&](auto n) { v.resize(n); };
    f(size_t(0));
    s.v.resize(3);
    auto g = [&]() { resetExt(s.v); };
    g();
    return v[2] + s.v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 12: a by-copy pointer written through.
int variant12_copy_pointer_written(HasVec *p) {
    if (!p || p->v.size() < 3) return 0;
    auto f = [p]() { p->v.clear(); };
    f();
    return p->v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 13: the pointee of a by-copy pointer can change before the call.
int variant13_pointee_changes(HasVec *p) {
    if (!p || p->v.size() < 3) return 0;
    auto f = [p]() { return p->v[2]; };  // dtt-error {{cannot verify|_size|out of bounds}}
    p->v.clear();
    return f();
}

// Variant 14: what a pointer member of a by-copy struct points to can change
// before the call.
struct VecRef { HasVec *h; };
int variant14_member_pointee_changes(VecRef s) {
    if (!s.h || s.h->v.size() < 3) return 0;
    auto f = [s]() { return s.h->v[2]; };  // dtt-error {{cannot verify|_size|out of bounds}}
    s.h->v.clear();
    return f();
}

// Variant 15: a mutable member of a by-copy capture changes between calls.
struct Counter { mutable int k; void bump() const { k = -1; } };
void variant15_mutable_member() {
    Counter x;
    x.k = 1;
    auto f = [x]() { take(x.k); x.bump(); };  // dtt-error {{cannot verify|_value}}
    f();
    f();
}

// Variant 16: a pointer init capture written through.
int variant16_init_capture_pointer(HasVec *p) {
    if (!p || p->v.size() < 3) return 0;
    auto f = [q = p]() { q->v.clear(); };
    f();
    return p->v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 17: a constructor runs the lambda it is handed.
struct Runner { template <class F> Runner(F f) { f(); } };
void variant17_constructor_runs() {
    int n = 5;
    Runner r([&] { n = -1; });
    take(n);  // dtt-error {{cannot verify|_value}}
}

// Variant 18: a destructor runs the lambda at the end of its scope.
template <class F> struct Guard { F f; ~Guard() { f(); } };
void variant18_destructor_runs() {
    int n = 5;
    {
        auto l = [&] { n = -1; };
        Guard<decltype(l)> g{l};
    }
    take(n);  // dtt-error {{cannot verify|_value}}
}

// Variant 19: a copy holds a value that names a by-reference capture, which
// changes before the call.
void variant19_seed_names_capture(int m) {
    int k = m;
    auto l = [k, &m] { if (m >= 0) take(k); };  // dtt-error {{cannot verify|_value}}
    m = 1;
    l();
}

// Variant 20: the same with a global.
int g_seed;
void variant20_seed_names_global() {
    int k = g_seed;
    auto l = [k] { if (g_seed >= 0) take(k); };  // dtt-error {{cannot verify|_value}}
    g_seed = 1;
    l();
}

// Variant 21: a moved-from closure's captured vector is moved-from.
int variant21_moved_closure() {
    std::vector<int> v(3);
    auto l = [v] { return v[0]; };  // dtt-error {{cannot verify|_size}}
    auto l2 = std::move(l);
    (void)l2;
    return l();
}

// Variant 22: the lambda closes the stream, then the caller closes it again.
void variant22_closed_by_lambda_then_caller() {
    FILE *f = fopen("a", "r");
    if (!f) return;
    auto l = [f] { fclose(f); };
    l();
    fclose(f);  // dtt-error {{_nonnull|closed}}
}

// Variant 23: the caller closes the stream, then the lambda closes it again.
void variant23_closed_by_caller_then_lambda() {
    FILE *f = fopen("a", "r");
    if (!f) return;
    auto l = [f] { fclose(f); };  // dtt-error {{_nonnull|closed}}
    fclose(f);
    l();
}

// Variant 24: a writer escapes after the reader is written; the reader's
// call to tick() may run it.
void registerCb(std::function<void()> f);
void tick();
void variant24_escape_after_reader() {
    int n = 0;
    auto reader = [&] { n = 5; tick(); take(n); };  // dtt-error {{cannot verify|_value}}
    registerCb([&] { n = -1; });
    reader();
}

// Variant 25: a lambda called in a loop moves the loop's end pointer.
void variant25_loop_end_written() {
    int a[4] = {};
    int *end = a + 4;
    auto grow = [&] { end = a + 100; };
    for (int *p = a; p < end; ++p) {
        *p = 1;  // dtt-error {{cannot verify|out of bounds|_offset}}
        grow();
    }
}

// Variant 26: a lambda called in a loop raises the loop's integer bound.
void variant26_loop_bound_written() {
    int a[4] = {};
    int n = 4;
    auto grow = [&] { n = 100; };
    for (int i = 0; i < n; ++i) {
        a[i] = 1;  // dtt-error {{cannot verify|out of bounds}}
        grow();
    }
}

// Variant 27: a lambda steps the loop's pointer a second time per pass.
void variant27_loop_pointer_stepped() {
    int a[4] = {};
    int *p = a;
    auto adv = [&] { ++p; };
    for (int i = 0; i < 4; ++i) {
        *p = 1;  // dtt-error {{cannot verify|out of bounds|_offset|nullability unknown}}
        ++p;
        adv();
    }
}

// Variant 28: a by-copy struct's pointer member written through.
struct Box { HasVec *p; };
int variant28_copy_struct_pointer_member(Box b) {
    if (!b.p || b.p->v.size() < 3) return 0;
    auto f = [b] { b.p->v.clear(); };
    f();
    return b.p->v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 29: a by-reference init capture whose object has no single path.
void variant29_reference_init_capture_choice(bool c) {
    int n = 5, m = 5;
    auto f = [&r = c ? n : m] { r = -1; };
    f();
    take(n);  // dtt-error {{cannot verify|_value}}
}

// Variant 30: a copy constructor that does not copy.
struct Resets {
    int n;
    Resets(int v) : n(v) {}
    Resets(const Resets &) : n(-1) {}
};
void variant30_copy_constructor(Resets t) {
    if (t.n < 0) return;
    auto f = [t] { take(t.n); };  // dtt-error {{cannot verify|_value}}
    f();
}

int main() { return 0; }
