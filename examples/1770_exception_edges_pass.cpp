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

int main() { return 0; }
