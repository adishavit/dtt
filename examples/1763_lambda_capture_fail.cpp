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

int main() { return 0; }
