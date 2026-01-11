// WHAT IS TESTED:
// Guards on expression return values (size() - 1) are not propagated.
//
// Expected behavior: PASS (size() - 1 >= 0 implies size() > 0)
// Current behavior: FAIL (no proof from expression guard)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:returns=_size")]]
    int size() const { return _size; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test() {
    Vec v;
    v._size = 1;

    if (v.size() - 1 >= 0) {
        v.front();
    }
}

int main() { test(); return 0; }
