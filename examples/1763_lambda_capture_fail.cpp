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

int main() { return 0; }
