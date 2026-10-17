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

int main() { return 0; }
