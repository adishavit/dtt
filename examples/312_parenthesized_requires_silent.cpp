// WHAT IS TESTED:
// Parenthesized constraints are not parsed, so requires are silently ignored.
//
// Expected behavior: ERROR (front() requires _size > 0)
// Current behavior: SILENT (constraint ignored due to parentheses)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=(_size > 0)")]]
    int& front() { return data[0]; }
};

void test() {
    Vec v;
    v._size = 0;
    v.front();
}

int main() { test(); return 0; }
