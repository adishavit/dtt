// WHAT IS TESTED:
// Void postconditions with argument expressions are not substituted.
//
// Expected behavior: PASS (v._size = count + 1 after init)
// Current behavior: FAIL (arg1 not substituted in void postcondition)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }
};

[[clang::annotate("dtt:ensures=result._size == arg1 + 1")]]
void init(Vec& v, int count) {
    (void)v;
    (void)count;
}

void test() {
    Vec v;
    v._size = 0;
    init(v, 1);
    v.at1();
}

int main() { test(); return 0; }
