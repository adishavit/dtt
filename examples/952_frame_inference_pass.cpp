// 952_frame_inference_pass.cpp
// Tests: Frame inference - proofs about untouched fields survive function calls
//
// When we have a proof about field X, and call a function that only
// touches field Y, the proof about X survives (preserved in the "frame").
//
// DTT tracks which fields each function writes. Fields NOT written are preserved.
//
// Expected: Only test_frame_invalidated should warn (clear() writes _size).

#include <cstdlib>

// Annotated vector for DTT
struct Vec {
    int* _data;
    int _size;
    int _capacity;
};

[[clang::annotate("dtt:requires=_size > 0")]]
int front(Vec& v) { return v._data[0]; }

// This function ONLY modifies _capacity, NOT _size
// Frame: {_size, _data} are preserved
void reserve_only(Vec& v, int new_cap) {
    if (new_cap > v._capacity) {
        v._data = (int*)realloc(v._data, new_cap * sizeof(int));
        v._capacity = new_cap;  // Only _capacity is written
        // _size is NOT touched - proof should survive
    }
}

// Test 1: Proof should survive call that doesn't touch it
// Expected: PASS - v._size proof survives call to reserve_only
void test_frame_survives() {
    Vec v;
    v._size = 5;           // Proof: v._size = 5, therefore v._size > 0
    reserve_only(v, 10);   // Frame: _size is preserved
    front(v);              // Should PASS: v._size > 0 proof survives
}

// This function modifies BOTH _size and _data
void clear(Vec& v) {
    v._size = 0;           // Writes _size
    // _data remains but _size is modified
}

// Test 2: Proof should NOT survive call that touches it
// Expected: FAIL - v._size proof is invalidated by clear()
void test_frame_invalidated() {
    Vec v;
    v._size = 5;           // Proof: v._size > 0
    clear(v);              // Invalidates _size proof
    front(v);              // Should FAIL: v._size is now 0
}

// This is a pure function - only reads, no writes
// Frame: all fields preserved
int total_capacity(const Vec& v1, const Vec& v2) {
    return v1._capacity + v2._capacity;
}

// Test 3: Proof should survive pure function call
// Expected: PASS - pure functions don't invalidate anything
void test_pure_frame() {
    Vec v1, v2;
    v1._size = 3;
    v2._size = 5;
    int cap = total_capacity(v1, v2);  // Pure - frame preserves everything
    front(v1);  // Should PASS: v1._size > 0
    front(v2);  // Should PASS: v2._size > 0
}

// Helper that increments _size (modifies it)
void increment_size(Vec& v) {
    v._size++;  // Writes _size
}

// Test 4: Proof about original value lost, but new proof established
// Expected: PASS - increment establishes v._size >= 1
void test_frame_with_increment() {
    Vec v;
    v._size = 5;           // Proof: v._size = 5
    increment_size(v);     // v._size becomes 6 - new value
    front(v);              // Should PASS: v._size was 5, now 6, still > 0
}

// Test 5: Multiple calls with different frames
void modify_data_only(Vec& v, int* new_data) {
    v._data = new_data;    // Only writes _data
}

void test_multiple_frames() {
    Vec v;
    v._size = 10;

    int arr[20];
    modify_data_only(v, arr);  // Frame: _size preserved

    reserve_only(v, 100);      // Frame: _size preserved

    front(v);  // Should PASS: _size proof survived both calls
}

int main() {
    test_frame_survives();
    test_frame_invalidated();
    test_pure_frame();
    test_frame_with_increment();
    test_multiple_frames();
    return 0;
}
