// 1770: A handler does not see a pointer as null when an argument makes it
// non-null before the callee may throw. Run in quiet mode with -Werror: a
// pointer that may be either is not a proven null dereference.

void throwing(int);

// Variant 1: an argument assigns a non-null pointer before the callee throws.
int variant1_argument_assigns_nonnull() {
    int x = 1;
    int *p = nullptr;
    try {
        throwing((p = &x, 0));
    } catch (...) {
        return *p;
    }
    return 0;
}

// Variant 2: the same, with the pointer read without a check after a call
// that cannot throw.
void noThrow(int) noexcept;
int variant2_nothrow_callee() {
    int x = 1;
    int *p = &x;
    try {
        noThrow((p = &x, 0));
        throwing(0);
    } catch (...) {
        return *p;
    }
    return 0;
}

// Variant 3: an inner catch-all handles everything; the outer handler only
// sees what the inner handler throws.
int variant3_inner_catch_all() {
    int x = 1;
    int *p = &x;
    try {
        try {
            p = nullptr;
            throwing(0);
        } catch (...) {
            p = &x;
            throwing(1);
        }
    } catch (...) {
        return *p;
    }
    return 0;
}

// Variant 4: a method that makes an inherited field non-null before it
// throws: the handler does not see it as null.
int y4;
struct BaseHolder { int *_p; };
struct DerivedHolder : BaseHolder {
    void setThenThrow(bool c) {
        _p = &y4;
        if (c) throw 1;
        _p = &y4;
    }
};
int variant4_inherited_field_set(bool c) {
    DerivedHolder s;
    s._p = nullptr;
    try {
        s.setThenThrow(c);
    } catch (...) {
        return *s._p;
    }
    return 0;
}

int main() { return 0; }
