// WHAT IS TESTED:
// Compound null guards with && should prove non-null inside branch.
//
// Expected: PASS

struct Data {
    int value;
    bool ready;
};

// Basic compound guard: ptr && ptr->ready
void test_basic_compound() {
    Data* ptr = new Data();
    ptr->ready = true;

    if (ptr && ptr->ready) {
        ptr->value = 1;  // ptr proven non-null by first part of &&
    }
}

// Multiple && conditions
void test_triple_and() {
    Data* ptr = new Data();
    ptr->ready = true;
    ptr->value = 0;

    if (ptr && ptr->ready && ptr->value == 0) {
        ptr->value = 1;
    }
}

// Compound with method call
struct Container {
    Data* data;
    bool hasData() { return data != nullptr; }
};

void test_compound_with_method() {
    Container c;
    c.data = new Data();

    if (c.data && c.data->ready) {
        c.data->value = 1;
    }
}

// Nested compound guards
void test_nested_compound() {
    Data* ptr1 = new Data();
    Data* ptr2 = new Data();

    if (ptr1 && ptr2) {
        if (ptr1->ready && ptr2->ready) {
            ptr1->value = ptr2->value;
        }
    }
}

int main() {
    test_basic_compound();
    test_triple_and();
    test_compound_with_method();
    test_nested_compound();
    return 0;
}
