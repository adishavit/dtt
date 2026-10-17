// 1772: A call sees the values its arguments give (quiet mode, -Werror:
// nothing about these calls is a proven violation).

static int read(int *p) { return *p; }

// Variant 1: an argument assigns a non-null pointer.
int variant1_assign_nonnull_in_argument() {
    int x = 1;
    int *p = nullptr;
    return read(p = &x);
}

// Variant 2: a postfix argument passes the value before the step.
[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
void variant2_postfix_argument() {
    int n = 0;
    take(n--);
    int m = -1;
    take(++m);
}

int main() { return 0; }
