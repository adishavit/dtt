// 1767: What a call does to its arguments, from the callee's paths (safe uses)

#include <cstdio>

struct Vec { int *data; int _size; };
[[clang::annotate("dtt:ensures=arg0._size >= arg1")]] void initVec(Vec &v, int n);
[[clang::annotate("dtt:ensures=arg0._size == arg1")]] void initEq(Vec &v, int n);
[[clang::annotate("dtt:requires=_size > 0")]] void needsNonEmpty(Vec &v);

// Variant 1: contracts with >= and == establish the field.
void variant1_ensures_bounds() {
    Vec v{};
    initVec(v, 10);
    needsNonEmpty(v);
    Vec w{};
    initEq(w, 10);
    needsNonEmpty(w);
}

struct B {
    int *data;
    int _capacity;
    [[clang::annotate("dtt:ensures=_capacity >= arg0")]] B &reserve(int n);
};
[[clang::annotate("dtt:requires=_capacity >= 100")]] void needsCapacity(B &b);
void variant2_reserve() {
    B b{};
    b.reserve(100);
    needsCapacity(b);
}

// Variant 3: a callee's write on a path the call does not take.
struct Sized {
    int *data;
    int _size;
    [[clang::annotate("dtt:requires=_size > 0")]] void need() {}
    void maybeClear(bool c) { if (c) _size = 0; }
    void deadCode() { return; _size = 0; }
    void loopDec() { while (_size > 1) _size--; }
    void setClamped(int n) { if (n < 1) n = 1; _size = n; }
};
void variant3_path_not_taken(bool f) {
    Sized s{};
    s._size = 5;
    s.maybeClear(f);
    if (f) return;
    s.deadCode();
    s.setClamped(0);
    s.need();
}

// Variant 4: a callee guards its parameter before dereferencing it.
static void earlyNull(int *p) { if (!p) return; *p = 1; }
static void condDeref(int *p, bool flag) { if (flag) *p = 1; }
static void deadDeref(int *p) { if (false) *p = 1; }
static void reassigned(int *p) { int local = 0; if (!p) p = &local; *p = 1; }
static void wrap(int *p, bool f) { condDeref(p, f); }
void variant4_guarded_callees() {
    earlyNull(nullptr);
    condDeref(nullptr, false);
    deadDeref(nullptr);
    reassigned(nullptr);
    wrap(nullptr, false);
}

// Variant 5: a pointer's nullness at the call, not at its declaration.
static void deref(int *p) { *p = 1; }
void variant5_nullness_at_call(bool c) {
    int x;
    int *p = nullptr;
    p = &x;
    deref(p);
    int *q = nullptr;
    if (c) q = &x;
    if (q) deref(q);
}

// Variant 6: a stream checked where it is used.
static int rd(FILE *f) { return fgetc(f); }
int variant6_stream_checked() {
    FILE *f = NULL;
    f = fopen("x", "r");
    if (f) {
        int c = rd(f);
        fclose(f);
        return c;
    }
    return 0;
}

int main() { return 0; }
