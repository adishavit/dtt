// Category F: Provably unsafe - always error regardless of mode
//
// This test verifies that provably unsafe code (Category F) always
// produces an error, regardless of the DTT mode setting.
// The mode only affects Categories C and D, not F.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear();
};

void test() {
    Vec v;
    v._size = 10;
    v.clear();      // v._size = 0

    v.front();      // ERROR (Category F): _size = 0, requires > 0  dtt-error {{_size > 0}}
                    // This is PROVEN unsafe, always an error
}

int main() { test(); return 0; }
