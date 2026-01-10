// Example 451: Struct pointer member array OOB — should FAIL
// Tests OOB detection through struct pointer access

struct Data {
    int values[8];
};

void test_struct_pointer_oob(Data* d) {
    int* p = d->values;    // _size=8
    p[8] = 42;             // FAIL: 8 >= 8 (one past end)
}

int main() {
    Data d;
    test_struct_pointer_oob(&d);
    return 0;
}
