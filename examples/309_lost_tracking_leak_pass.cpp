// WHAT IS TESTED:
// Lost-tracking provenance should be scoped to a single function body.
// This test demonstrates the current leak of FieldStates/WarnedLostTracking
// across functions that reuse variable names.
//
// Expected behavior: PASS (second function has no tracking, so no Category C)
// Current behavior: FAIL (Category C leak from prior function)

void unknown(struct Vec& v);  // Unannotated - causes tracking loss

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void first() {
    int arr[1] = {};
    Vec v;
    v.data = arr;
    v._size = 1;
    unknown(v);  // Tracking lost for v._size
}

void second() {
    int arr[1] = {};
    Vec v;
    v.data = arr;
    // No tracking established here - should be treated as untracked, not lost
    v.front();
}

int main() {
    first();
    second();
    return 0;
}
