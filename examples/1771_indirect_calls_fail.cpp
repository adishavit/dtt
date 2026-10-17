// 1771: A call DTT cannot resolve to one definition writes what its
// parameter types let it write.

// Variant 1: a function pointer taking a pointer by reference.
int variant1_function_pointer_reference(void (*fn)(int *&)) {
    int x = 1;
    int *p = &x;
    fn(p);
    return *p;  // dtt-error {{null}}
}

// Variant 2: a function pointer taking a pointer to the pointer.
int variant2_function_pointer_pointer(void (*fn)(int **)) {
    int x = 1;
    int *p = &x;
    fn(&p);
    return *p;  // dtt-error {{null}}
}

// Variant 3: a function pointer taking a scalar by reference.
[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
void variant3_function_pointer_scalar(void (*fn)(int &)) {
    int n = 5;
    fn(n);
    take(n);  // dtt-error {{cannot verify|_value}}
}

// Variant 4: a virtual call runs whichever override the object has; the
// base's body is not what it does.
struct Base {
    int *p;
    virtual void reset(int *&q) { (void)q; }
    virtual ~Base() = default;
};
struct Derived : Base {
    void reset(int *&q) override { q = nullptr; }
};
int variant4_virtual_override(Base &b) {
    int x = 1;
    int *p = &x;
    b.reset(p);
    return *p;  // dtt-error {{null}}
}

// Variant 5: the base's body making the pointer non-null is not what an
// override does.
int g_target;
struct Setter {
    virtual void set(int *&q) { q = &g_target; }
    virtual ~Setter() = default;
};
struct Clearer : Setter {
    void set(int *&q) override { q = nullptr; }
};
int variant5_virtual_base_effect(Setter &s) {
    int x = 1;
    int *p = &x;
    s.set(p);
    return *p;  // dtt-error {{null}}
}

// Variant 6: a pointer to a member function, called with .*.
struct Resetter {
    int *own;
    void reset(int *&q) { q = nullptr; }
    void clearOwn() { own = nullptr; }
};
int variant6_member_pointer_dot(Resetter &s) {
    int x = 1;
    int *p = &x;
    void (Resetter::*fn)(int *&) = &Resetter::reset;
    (s.*fn)(p);
    return *p;  // dtt-error {{null}}
}

// Variant 7: the same with ->*.
int variant7_member_pointer_arrow(Resetter *q) {
    if (!q) return 0;
    int x = 1;
    int *p = &x;
    void (Resetter::*fn)(int *&) = &Resetter::reset;
    (q->*fn)(p);
    return *p;  // dtt-error {{null}}
}

// Variant 8: a member function called through a pointer may write its object.
int variant8_member_pointer_object() {
    int x = 1;
    Resetter s{&x};
    void (Resetter::*fn)() = &Resetter::clearOwn;
    (s.*fn)();
    return *s.own;  // dtt-error {{null}}
}

int main() { return 0; }
