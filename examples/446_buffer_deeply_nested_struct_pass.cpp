// Example 446: Deeply nested struct arrays — should COMPILE
// Tests array members in deeply nested struct hierarchies

struct Level3 {
    char data[16];
};

struct Level2 {
    Level3 inner;
    int values[8];
};

struct Level1 {
    Level2 mid;
    double coords[4];
};

struct Root {
    Level1 child;
    char name[64];
};

void test_deep_nesting() {
    Root r;

    // Access array at each nesting level
    char* d = r.child.mid.inner.data;  // _size=16
    d[15] = '\0';                       // OK: last char

    int* v = r.child.mid.values;       // _size=8
    v[7] = 42;                          // OK: last int

    double* c = r.child.coords;        // _size=4
    c[3] = 1.0;                         // OK: last double

    char* n = r.name;                  // _size=64
    n[63] = '\0';                       // OK: last char
}

int main() {
    test_deep_nesting();
    return 0;
}
