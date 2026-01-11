// WHAT IS TESTED:
// Method constraints are skipped when the implicit object is a temporary.
//
// Expected behavior: ERROR (front() requires _size > 0)
// Current behavior: SILENT (no object path for temporary)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

Vec makeEmpty() {
    Vec v;
    v._size = 0;
    return v;
}

void test() {
    makeEmpty().front();
}

int main() { test(); return 0; }
