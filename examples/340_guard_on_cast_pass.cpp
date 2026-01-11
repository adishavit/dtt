// WHAT IS TESTED:
// Guards on casted pointers should prove non-null inside guard.
//
// Expected: PASS

struct Data {
    int value;
};

struct Derived : Data {
    int extra;
};

// static_cast guard
void test_static_cast(void* raw) {
    if (static_cast<Data*>(raw) != nullptr) {
        static_cast<Data*>(raw)->value = 1;
    }
}

// reinterpret_cast guard
void test_reinterpret_cast(void* raw) {
    if (reinterpret_cast<Data*>(raw) != nullptr) {
        reinterpret_cast<Data*>(raw)->value = 2;
    }
}

// Note: dynamic_cast requires polymorphic types, skipped here

// C-style cast guard
void test_c_style_cast(void* raw) {
    if ((Data*)raw != nullptr) {
        ((Data*)raw)->value = 3;
    }
}

int main() {
    Data d;
    test_static_cast(&d);
    test_reinterpret_cast(&d);
    test_c_style_cast(&d);
    return 0;
}
