// Category G: Annotation conflict - conflicting ensures (should always error)
//
// This test verifies that DTT detects when multiple ensures annotations
// set the same field to different values. This is always an error regardless
// of mode (inform/enforce/quiet) because it's a configuration bug.

struct Vec {
    int _size;

    // ERROR: Conflicting ensures - _size can't be both 0 and 10
    [[clang::annotate("dtt:ensures=_size == 0")]]
    [[clang::annotate("dtt:ensures=_size == 10")]]
    void confuse();
};

int main() {
    Vec v;
    v.confuse();
    return 0;
}
