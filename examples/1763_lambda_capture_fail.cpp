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

int main() { return 0; }
