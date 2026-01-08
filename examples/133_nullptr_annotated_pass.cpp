// Example 133: annotated function with nullptr constraint - should COMPILE
// Tests that functions can require non-null arguments

struct Data {
    int value;
};

[[clang::annotate("dtt:requires=arg0 != nullptr")]]
void processData(Data* data) {
    // Caller guarantees data is not null
}

[[clang::annotate("dtt:ensures=result._nonnull = 1")]]
Data* createData() {
    return new Data();  // Always returns non-null
}

int main() {
    // This should work because new returns non-null
    Data* ptr = new Data();
    processData(ptr);  // OK: ptr is known non-null from new

    // This should also work because createData ensures non-null
    Data* ptr2 = createData();
    ptr2->value = 42;  // OK: createData guarantees non-null

    delete ptr;
    delete ptr2;
    return 0;
}
