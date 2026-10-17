// 1770: A call inside a try block may throw after its arguments are
// evaluated, and after it writes what it may write: the handler sees that.

void throwing(int);
int mayThrow();

// Variant 1: an argument assigns null before the callee throws.
int variant1_argument_assigns_null() {
    int x = 1;
    int *p = &x;
    try {
        throwing((p = nullptr, 0));
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 2: an argument that itself throws, after an assignment.
int variant2_argument_throws() {
    int x = 1;
    int *p = &x;
    try {
        throwing((p = nullptr, mayThrow()));
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 3: a callee that writes a referenced pointer, then throws.
int y3;
void resetThenThrow(int *&q, bool c) {
    q = nullptr;
    if (c) throw 1;
    q = &y3;
}
int variant3_callee_writes_then_throws(bool c) {
    int x = 1;
    int *p = &x;
    try {
        resetThenThrow(p, c);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return *p;
}

// Variant 4: a method that writes its object, then throws.
struct Holder {
    int *p;
    void resetThenThrow(bool c) {
        p = nullptr;
        if (c) throw 1;
        p = &y3;
    }
};
int variant4_method_writes_then_throws(bool c) {
    int x = 1;
    Holder h{&x};
    try {
        h.resetThenThrow(c);
    } catch (...) {
        return *h.p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 5: a constructor that writes a referenced pointer, then throws.
struct Resets {
    Resets(int *&q, bool c) {
        q = nullptr;
        if (c) throw 1;
        q = &y3;
    }
};
int variant5_constructor_writes_then_throws(bool c) {
    int x = 1;
    int *p = &x;
    try {
        Resets r(p, c);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 6: an operator's argument assigns null before the operator throws.
struct Sink { Sink &operator<<(int); };
int variant6_operator_argument(Sink &s) {
    int x = 1;
    int *p = &x;
    try {
        s << (p = nullptr, 0);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 7: an indirect call that writes a referenced pointer, then throws.
void resetRefThenThrow(int *&q) { q = nullptr; throw 1; }
int variant7_indirect_reference() {
    int x = 1;
    int *p = &x;
    void (*fn)(int *&) = resetRefThenThrow;
    try {
        fn(p);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 8: the same through a pointer to the pointer.
void resetPtrThenThrow(int **q) { *q = nullptr; throw 1; }  // dtt-error {{null}}
int variant8_indirect_pointer_to_pointer() {
    int x = 1;
    int *p = &x;
    void (*fn)(int **) = resetPtrThenThrow;
    try {
        fn(&p);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

int main() { return 0; }
