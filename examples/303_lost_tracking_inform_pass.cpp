// Category C: Lost tracking - inform mode behavior
//
// In inform mode (default), DTT emits a WARNING when tracking is lost
// and a potentially unsafe operation is performed. Since warnings don't
// block compilation, this test passes but should produce diagnostic output.
//
// This test documents the expected behavior:
// 1. v._size = 10 - tracking established
// 2. unknown(v) - tracking lost (Category C)
// 3. v.front() - WARNING: cannot verify constraint (tracking was lost)
//                NOTE: tracking was lost here: call to unannotated function 'unknown()'
//
// The test passes because warnings don't block compilation.

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
    v._size = 10;  // Known: v._size = 10

    unknown(v);    // Tracking lost here (Category C)

    v.front();     // In inform mode: WARNING, but compiles
                   // In enforce mode: ERROR
                   // In quiet mode: silent
}

int main() { test(); return 0; }
