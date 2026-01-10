// Example 442: Nested struct with array member — should COMPILE
// Tests that array members in nested structs are tracked

struct Inner {
    char name[32];
};

struct Outer {
    int id;
    Inner inner;
    double values[8];
};

void test_nested_struct() {
    Outer o;
    char* name = o.inner.name;  // _size=32
    name[31] = '\0';            // OK: last char

    double* vals = o.values;    // _size=8
    vals[7] = 1.0;              // OK: last double
}

struct Container {
    int header[4];
    char payload[256];
    int footer[4];
};

void test_multiple_arrays() {
    Container c;
    int* h = c.header;     // _size=4
    char* p = c.payload;   // _size=256
    int* f = c.footer;     // _size=4

    h[3] = 0xDEAD;         // OK: last header
    p[255] = 0;            // OK: last payload
    f[3] = 0xBEEF;         // OK: last footer
}

int main() {
    test_nested_struct();
    test_multiple_arrays();
    return 0;
}
