// WHAT IS TESTED:
// Auto-postcondition inference for loops.
// Gap: DTT has no loop analysis or widening.
//
// EXPECTED: DTT infers loop invariants or final state.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size >= n")]]
    void require_n(int n) { }

    // Gap: Simple counting loop
    void fill_to(int n) {
        while (_size < n) {
            _size++;
        }
        // Post: _size >= n
    }

    // Gap: Decrement loop
    void drain() {
        while (_size > 0) {
            _size--;
        }
        // Post: _size == 0
    }

    // Gap: For loop increment
    void add_n(int n) {
        for (int i = 0; i < n; i++) {
            _size++;
        }
        // Post: _size == old(_size) + n
    }

    // Gap: Bounded loop
    void add_at_most(int n, int limit) {
        for (int i = 0; i < n && _size < limit; i++) {
            _size++;
        }
        // Post: _size <= limit
    }
};

// Variant 1: Fill to target
void variant1_fill() {
    Vec v;
    v._size = 0;
    v.fill_to(5);
    v.require_n(5);  // OK: _size >= 5
}

// Variant 2: Drain to empty
void variant2_drain() {
    Vec v;
    v._size = 10;
    v.drain();
    v.front();  // BUG: _size == 0
}

// Variant 3: Add exact count
void variant3_add_n() {
    Vec v;
    v._size = 3;
    v.add_n(5);
    v.require_n(8);  // OK: _size == 8
}

// Variant 4: Bounded add
void variant4_bounded() {
    Vec v;
    v._size = 8;
    v.add_at_most(100, 10);
    v.require_n(10);  // OK: _size <= 10 and _size >= 8
}

int main() {
    variant1_fill();
    variant2_drain();
    variant3_add_n();
    variant4_bounded();
    return 0;
}
