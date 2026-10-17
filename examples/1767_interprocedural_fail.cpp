// 1767: What a call does to its arguments, from the callee's paths
//
// A callee with a body was assumed to leave its reference arguments alone
// unless it wrote a field by name; contracts with >=, <=, != were misread; a
// summary took the callee's last write or return whatever the path; and the
// nullness of an argument came from the variable's initializer.

#include <vector>

struct Vec { int *data; int _size; };
[[clang::annotate("dtt:requires=_size > 0")]] void needsNonEmpty(Vec &v);

// Variant 1: a callee that hands the argument to an unknown function.
void ext(Vec &v);
void wrapExt(Vec &v) { ext(v); }
void variant1_callee_calls_unknown() {
    Vec v{};
    v._size = 5;
    wrapExt(v);
    needsNonEmpty(v);  // dtt-error {{cannot verify}}
}

// Variant 2: a callee that writes the argument through a pointer.
void viaPtr(Vec &v) {
    Vec *p = &v;
    p->_size = 0;
}
void variant2_callee_writes_through_pointer() {
    Vec v{};
    v._size = 5;
    viaPtr(v);
    needsNonEmpty(v);  // dtt-error {{cannot verify|_size = 0}}
}

// Variant 3: a callee that clears a container.
void clearIt(std::vector<int> &v) { v.clear(); }
int variant3_callee_clears() {
    std::vector<int> v(3);
    clearIt(v);
    return v[0];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 4: a callee defined after its caller.
void later(Vec &v);
void variant4_callee_defined_later() {
    Vec v{};
    v._size = 5;
    later(v);
    needsNonEmpty(v);  // dtt-error {{cannot verify|_size = 0}}
}
void later(Vec &v) { v._size = 0; }

// Variant 5: contracts with <= and != bound the field.
struct B {
    int *data;
    int _capacity;
    [[clang::annotate("dtt:ensures=_capacity <= 5")]] void shrink();
    [[clang::annotate("dtt:ensures=_capacity != 200")]] void perturb();
};
[[clang::annotate("dtt:requires=_capacity >= 100")]] void needsCapacity(B &b);
void variant5_shrink() {
    B b{};
    b._capacity = 200;
    b.shrink();
    needsCapacity(b);  // dtt-error {{_capacity|cannot verify}}
}
void variant5_perturb() {
    B b{};
    b._capacity = 200;
    b.perturb();
    needsCapacity(b);  // dtt-error {{cannot verify}}
}

// Variant 6: a write on one path of the callee is not a write on every path.
struct Sized {
    int *data;
    int _size;
    [[clang::annotate("dtt:requires=_size > 0")]] void need() {}
    void condInc(bool c) { if (c) ++_size; }
    void maybeClear(bool c) { if (c) _size = 0; }
};
void variant6_conditional_increment(bool c) {
    Sized s{};
    s._size = 0;
    s.condInc(c);
    s.need();  // dtt-error {{cannot verify}}
}
void variant6_conditional_clear(bool c) {
    Sized s{};
    s._size = 5;
    s.maybeClear(c);
    s.need();  // dtt-error {{cannot verify}}
}

// Variant 7: a container a callee may grow.
void maybe_add(std::vector<int> &v, bool c) { if (c) v.push_back(1); }
int variant7_maybe_grown(bool c) {
    std::vector<int> v;
    maybe_add(v, c);
    return v[0];  // dtt-error {{cannot verify}}
}

// Variant 8: a pointer reassigned to null before it is passed on.
static void deref(int *p) { *p = 1; }
void variant8_reassigned_null() {
    int x;
    int *p = &x;
    p = nullptr;
    deref(p);  // dtt-error {{null}}
}

// Variant 9: a bound on a field does not prove the opposite bound.
[[clang::annotate("dtt:requires=_capacity <= 100")]] void needsSmall(B &b);
void variant9_opposite_bounds(B &b) {
    if (b._capacity <= 5) needsCapacity(b);  // dtt-error {{_capacity|cannot verify}}
    if (b._capacity >= 300) needsSmall(b);   // dtt-error {{_capacity|cannot verify}}
}

// Variant 10: a size over a parameter the body changes is not a requirement
// over the argument.
#include <cstring>
void variant10_reassigned_size(char *b, unsigned n) {
    if (n > 2) n = 9;
    memset(b, 0, n);  // dtt-error {{cannot verify|_size_bytes}}
}

// Variant 11: a parameter reassigned before it is dereferenced is not the
// argument: the pointer dereferenced is the one it was given.
static int derefOther(int *p, int *q) { p = q; return *p; }  // dtt-error {{null|cannot verify}}
int variant11_reassigned_parameter() {
    int x = 0;
    return derefOther(&x, nullptr);
}

// Variant 12: a library requirement that a pointer be non-null reaches the
// callers of a static wrapper that only steps the pointer.
#include <cstring>
static size_t lengthAfterFirst(const char *s) {
    s++;
    return strlen(s);
}
size_t variant12_stepped_parameter_requirement() {
    return lengthAfterFirst(nullptr);  // dtt-error {{null|_nonnull}}
}

int main() { return 0; }
