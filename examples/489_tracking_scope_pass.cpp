// Example 489: Tracking scope per function — should COMPILE
// Lost-tracking provenance is scoped to a single function body.
// Variables in different functions are independent even with same names.

void unknown(struct Vec& v);  // Unannotated - causes tracking loss

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void first() {
    int arr[] = {1};
    Vec v;
    v.data = arr;
    v._size = 1;
    unknown(v);  // Tracking lost for v in this function only
}

void second() {
    int arr[] = {1};
    Vec v;
    v.data = arr;
    v._size = 1;  // Establish tracking fresh - no leak from first()
    if (v._size > 0) {
        v.front();  // OK: proof established independently
    }
}

int main() {
    first();
    second();
    return 0;
}
