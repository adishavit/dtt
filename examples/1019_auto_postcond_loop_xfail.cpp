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

// Variant 2: Drain to empty
void variant2_drain() {
    Vec v;
    v._size = 10;
    v.drain();
    v.front();  // BUG: _size == 0
}


int main() { variant2_drain(); return 0; }
