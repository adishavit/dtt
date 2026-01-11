// WHAT IS TESTED:
// Void postconditions do not substitute argument values (arg1).
//
// Expected behavior: PASS (init should set _size = count)
// Current behavior: FAIL (arg1 not substituted, _size unknown)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }
};

[[clang::annotate("dtt:ensures=result._size == arg1")]]
void init(Vec& v, int count) {
    (void)v;
    (void)count;
}

void test() {
    Vec v;
    v._size = 0;
    init(v, 2);
    v.at1();
}

int main() { test(); return 0; }
