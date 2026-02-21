// WHAT IS TESTED:
// Logical OR chains don't propagate proofs to later branches.
//
// Expected behavior: PASS (if neither null nor empty, front() safe)
// Current behavior: FAIL (no proof in final branch)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test(Vec& v, bool flag) {
    if (v._size == 0 || flag) {
        return;
    }

    v.front();
}

int main() {
    int arr[] = {1};
    Vec v;
    v.data = arr;
    v._size = 1;
    test(v, false);
    return 0;
}
