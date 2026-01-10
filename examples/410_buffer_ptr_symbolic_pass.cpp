// Example 410: Buffer pointer symbolic bounds with guards — should COMPILE
// Tests symbolic tracking with if-guards that constrain runtime-known sizes

int get_size();

void test_symbolic_with_guard() {
    int n = get_size();
    int* p = new int[n];   // p: _size = n (symbolic)

    if (n > 0) {
        p[0] = 1;          // OK: Z3 proves 0 < n given n > 0
    }

    if (n > 5) {
        p[4] = 2;          // OK: Z3 proves 4 < n given n > 5
    }

    delete[] p;
}

int main() {
    test_symbolic_with_guard();
    return 0;
}
