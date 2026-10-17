// 1763: Calling a lambda writes what it captures by reference (safe uses)

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);

// Variant 1: the body makes a captured variable valid; DTT must not keep
// the old negative value as a proven violation (unknown is fine: this test
// runs in inform mode).
void variant1_fixed_by_lambda() {
    int n = -1;
    auto l = [&]() { n = 5; };
    l();
    take(n);
}

// Variant 2: a by-copy capture leaves the original alone.
void variant2_copy_capture() {
    int n = 5;
    auto l = [n]() mutable { n = -1; return n; };
    l();
    take(n);
}

int main() { return 0; }
