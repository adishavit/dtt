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

// Variant 9: an exception the inner handler may not match reaches the
// outer one.
int variant9_unmatched_inner_handler() {
    int x = 1;
    int *p = &x;
    try {
        try {
            p = nullptr;
            throw 1;
        } catch (double) {
        }
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 10: the same from a call that may throw anything.
int variant10_unmatched_from_call() {
    int x = 1;
    int *p = &x;
    try {
        try {
            p = nullptr;
            throwing(0);
        } catch (const char *) {
        }
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 11: a rethrow from the inner handler reaches the outer one.
int variant11_rethrow() {
    int x = 1;
    int *p = &x;
    try {
        try {
            throwing(0);
        } catch (...) {
            p = nullptr;
            throw;
        }
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 12: a method that writes an inherited field, then throws.
struct BaseHolder { int *_p; };
struct DerivedHolder : BaseHolder {
    void resetThenThrow(bool c) {
        _p = nullptr;
        if (c) throw 1;
        _p = &y3;
    }
};
int variant12_inherited_field(bool c) {
    DerivedHolder s;
    s._p = &y3;
    try {
        s.resetThenThrow(c);
    } catch (...) {
        return *s._p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 13: a contract's ghost state does not hold when the method throws.
struct Runner {
    [[clang::annotate("dtt:ensures=@ready == 1")]] void init();
    [[clang::annotate("dtt:ensures=@ready == 0")]] void clear();
    [[clang::annotate("dtt:ensures=@ready == 1")]] void run(bool c) {
        clear();
        if (c) throw 1;
        init();
    }
    [[clang::annotate("dtt:requires=@ready == 1")]] void use();
};
void variant13_ghost_state(bool c) {
    Runner r;
    r.init();
    try {
        r.run(c);
    } catch (...) {
        r.use();  // dtt-error {{@ready|cannot verify}}
    }
}

// Variant 14: a const method may write a mutable field before throwing.
struct Cache {
    mutable int *p;
    void resetThenThrow() const {
        p = nullptr;
        throw 1;
    }
};
int variant14_mutable_field() {
    int x = 1;
    Cache c{&x};
    try {
        c.resetThenThrow();
    } catch (...) {
        return *c.p;  // dtt-error {{null}}
    }
    return 0;
}

// Variant 15: a pointer to a member function that writes, then throws.
struct MemberThrower {
    void resetThenThrow(int *&q) { q = nullptr; throw 1; }
};
int variant15_member_pointer(MemberThrower &s) {
    int x = 1;
    int *p = &x;
    void (MemberThrower::*fn)(int *&) = &MemberThrower::resetThenThrow;
    try {
        (s.*fn)(p);
    } catch (...) {
        return *p;  // dtt-error {{null}}
    }
    return 0;
}

int main() { return 0; }
