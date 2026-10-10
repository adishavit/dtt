// WHAT IS TESTED:
// Auto-postcondition inference for pointer dereference modifications.
// Gap: DTT may not track *ptr = value assignments.
//
// EXPECTED: DTT tracks pointer dereference modifications.

struct Vec {
    int* data;
    int _size;
    int* size_ptr;  // Points to _size

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    Vec() : size_ptr(&_size) {}

    // Gap: Direct pointer dereference
    void clear_via_ptr() {
        *size_ptr = 0;
    }

    // Gap: Pointer arithmetic and deref
    void set_via_ptr(int n) {
        *size_ptr = n;
    }

    // Gap: Increment via pointer
    void inc_via_ptr() {
        (*size_ptr)++;
    }
};

// Variant 1: Clear via pointer
void variant1_clear_ptr() {
    Vec v;
    v._size = 5;
    v.clear_via_ptr();
    v.front();  // BUG: _size == 0  dtt-error {{_size > 0}}
}

// Variant 2: Set via pointer
void variant2_set_ptr() {
    Vec v;
    v._size = 0;
    v.set_via_ptr(10);
    v.front();  // OK: _size == 10 > 0
}

// Variant 3: Increment via pointer
void variant3_inc_ptr() {
    Vec v;
    v._size = 0;
    v.inc_via_ptr();
    v.front();  // OK: _size == 1 > 0
}

int main() {
    variant1_clear_ptr();
    variant2_set_ptr();
    variant3_inc_ptr();
    return 0;
}
