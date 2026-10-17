// 1772: A call is entered after its arguments are evaluated: what it
// requires and what it is passed are checked against that state.

static int read(int *p) { return *p; }

// Variant 1: an argument assigns null, then the callee dereferences it.
int variant1_assign_null_in_argument() {
    int x = 1;
    int *p = &x;
    return read(p = nullptr);  // dtt-error {{null}}
}

// Variant 2: the same through a comma expression.
int variant2_comma_argument() {
    int x = 1;
    int *p = &x;
    return read((p = nullptr, p));  // dtt-error {{null}}
}

// Variant 3: an annotated requirement on an argument assigned in the call.
[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
void variant3_requirement_after_argument() {
    int n = 5;
    take(n = -1);  // dtt-error {{violated|arg0 >= 0|cannot verify}}
}

// Variant 4: a nested call that clears the receiver before the outer call
// requires it non-empty.
struct Vec {
    int *data;
    int _size;
    [[clang::annotate("dtt:requires=_size > 0")]] int use(int k);
};
[[clang::annotate("dtt:ensures=arg0._size == 0")]] int clear(Vec &v);
int variant4_nested_argument_effect(Vec &s) {
    if (s._size < 1) return 0;
    return s.use(clear(s));  // dtt-error {{_size|cannot verify|violated}}
}

// Variant 5: an indirect call's argument assigns null.
int variant5_indirect_argument(int (*fn)(int *)) {
    int x = 1;
    int *p = &x;
    fn(p = nullptr);
    return *p;  // dtt-error {{null}}
}

int main() { return 0; }
