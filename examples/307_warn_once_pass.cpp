// Category C: Warn-once per variable per scope
//
// This test verifies that DTT only warns once per (variable, field)
// combination when tracking is lost, not on every subsequent use.
//
// Expected behavior in inform mode:
// 1. First v.front() - WARNING with note pointing to unknown()
// 2. Second v.front() - silent (already warned)
// 3. v.back() - WARNING (different method, same field, same warn-once)
//
// Actually, the warn-once is per (basePath, field), so both front() and
// back() check the same _size field - only one warning total.

void unknown(struct Vec& v);

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& back() { return data[_size - 1]; }
};

void test() {
    int arr[10] = {};
    Vec v;
    v.data = arr;
    v._size = 10;

    unknown(v);     // Tracking lost

    v.front();      // WARNING: tracking was lost (first use)
    v.front();      // Silent (warn-once)
    v.back();       // Silent (same field, already warned)
}

int main() { test(); return 0; }
