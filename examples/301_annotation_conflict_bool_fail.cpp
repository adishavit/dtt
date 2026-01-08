// Category G: Annotation conflict - conflicting boolean postconditions
//
// This test verifies that DTT detects when multiple ensures_if_true or
// ensures_if_false annotations for the same field have conflicting values.

struct Vec {
    int _size;

    // ERROR: Conflicting ensures_if_true - can't both be == 0 and > 0
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_true=_size > 0")]]
    bool isEmpty() const;
};

int main() {
    Vec v;
    v.isEmpty();
    return 0;
}
