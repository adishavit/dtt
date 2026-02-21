// Category C: Lost tracking - restored by re-assignment (should pass)
//
// This test verifies that tracking can be restored after being lost.
// After unknown(v) loses tracking, re-assigning v._size restores it.

void unknown(struct Vec& v);  // Unannotated - will cause tracking loss

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test() {
    int arr[10] = {};
    Vec v;
    v.data = arr;
    v._size = 10;   // Known: v._size = 10

    unknown(v);     // Tracking lost (Category C)

    v._size = 5;    // Tracking RESTORED: v._size = 5

    v.front();      // OK: v._size = 5 (tracking restored)
}

int main() { test(); return 0; }
