// 1762: Lambda bodies are analyzed (safe uses)

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);

// Variant 1: a guarded body.
int variant1_guarded(int *p) {
    auto l = [](int *q) {
        if (!q) return 0;
        return *q;
    };
    return l(p);
}

// Variant 2: a body that does not run where it is written.
void variant2_not_run_at_definition() {
    int n = 5;
    auto l = [&n]() { n = -1; };
    take(n);
    (void)l;
}

// Variant 3: the enclosing function continues where it was.
void variant3_outer_state_kept(int *p) {
    if (!p) return;
    auto l = []() {
        int *q = nullptr;
        return q != nullptr;
    };
    (void)l;
    *p = 1;
}

// Variant 4: an init capture runs where the lambda is written.
void variant4_init_capture() {
    int a[3] = {};
    auto l = [k = 2, &a]() { a[k] = 1; };
    l();
}

// Variant 5: an init capture's initializer is checked once, where it runs.
[[clang::annotate("dtt:requires=n >= 0")]] int needsPos(int n);
int variant5_init_capture_checked_once() {
    int a = 5;
    int x = 1;
    int *p = &x;
    auto l = [k = needsPos(a), v = *p]() { return k + v; };
    return l();
}

// Variant 6: an init capture named like a local does not touch the local.
#include <vector>
int variant6_init_capture_name() {
    std::vector<int> w(3);
    auto g = [w = std::vector<int>()]() { return w.size(); };
    (void)g;
    return w[2];
}

// Variant 7: a by-copy capture keeps what was known where the lambda was
// written; an init capture of a resource is the body's.
#include <cstdio>
int variant7_copy_capture(int *q) {
    if (!q) return 0;
    auto f = [q]() { return *q; };
    auto h = [fp = fopen("x", "r")]() {
        if (fp) fclose(fp);
    };
    h();
    return f();
}

// Variant 8: [*this] mutates its own copy.
struct Owner {
    std::vector<int> v;
    int variant8_star_this() {
        if (v.size() < 3) return 0;
        auto f = [*this]() mutable { v.clear(); };
        f();
        return v[2];
    }
};

// Variant 9: a lambda that only reads a container leaves it alone.
#include <algorithm>
int variant9_read_only_escape(std::vector<int> &w) {
    std::vector<int> v(3);
    int n = 0;
    std::for_each(w.begin(), w.end(), [&](int x) { n += x + static_cast<int>(v.size()); });
    v.resize(4);
    return v[3] + n;
}

int main() { return 0; }
