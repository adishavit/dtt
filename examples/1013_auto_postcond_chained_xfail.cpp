// WHAT IS TESTED:
// Auto-postcondition inference with method chaining.
// Gap: DTT may lose intermediate state between chained calls.
//
// EXPECTED: DTT tracks state through entire chain.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    // Returns *this for chaining
    Vec& clear() {
        _size = 0;
        return *this;
    }

    Vec& resize(int n) {
        _size = n;
        return *this;
    }

    Vec& push() {
        _size++;
        return *this;
    }

    Vec& pop() {
        _size--;
        return *this;
    }
};

// Variant 1: Clear then resize (should be safe)
void variant1_clear_resize() {
    Vec v;
    v._size = 5;
    v.clear().resize(10);  // Final: _size = 10
    v.front();  // OK: 10 > 0
}

// Variant 2: Resize then clear (should be unsafe)
void variant2_resize_clear() {
    Vec v;
    v._size = 0;
    v.resize(10).clear();  // Final: _size = 0
    v.front();  // BUG: _size == 0
}

// Variant 3: Multiple pushes
void variant3_chain_push() {
    Vec v;
    v._size = 0;
    v.push().push().push().push().push();  // _size = 5
    v.at4();  // OK: 5 > 4
}

// Variant 4: Push and pop chain
void variant4_push_pop() {
    Vec v;
    v._size = 3;
    v.push().push().pop();  // 3 + 1 + 1 - 1 = 4
    v.front();  // OK: 4 > 0
}

// Variant 5: Complex chain ending unsafe
void variant5_complex_unsafe() {
    Vec v;
    v._size = 10;
    v.clear().push().pop();  // 0 + 1 - 1 = 0
    v.front();  // BUG: _size == 0
}

int main() {
    variant1_clear_resize();
    variant2_resize_clear();
    variant3_chain_push();
    variant4_push_pop();
    variant5_complex_unsafe();
    return 0;
}
