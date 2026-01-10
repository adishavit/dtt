// Example 450: Struct accessed through pointer — should COMPILE
// Tests array member tracking through struct pointers

struct Data {
    int values[16];
};

void test_struct_pointer(Data* d) {
    if (d) {
        int* p = d->values;    // _size=16
        p[15] = 42;            // OK: last element
    }
}

void test_new_struct() {
    Data* d = new Data;
    int* p = d->values;    // _size=16
    p[0] = 1;              // OK: first element
    delete d;
}

int main() {
    Data d;
    test_struct_pointer(&d);
    test_new_struct();
    return 0;
}
