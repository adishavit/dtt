// WHAT IS TESTED:
// Cast guard should not apply to different cast result.
//
// Expected: ERROR

struct Data {
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return *reinterpret_cast<int*>(0); }
};

// Guard on one cast, access on same cast but without guard
void test_wrong_context() {
    Data d;
    d._size = 0;
    d.front();  // ERROR: _size = 0, not > 0  dtt-error {{_size > 0}}
}

int main() { test_wrong_context(); return 0; }
