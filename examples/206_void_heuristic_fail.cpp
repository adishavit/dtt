// A mutable reference passed to an external function may be modified.
// The later requirement needs a new proof; the function name implies no purity.

struct Data {
    int* values;
    int _count;
};

// No visible implementation or contract establishes that this function is pure.
void printData(Data& d);  // No DTT annotation, returns void

[[clang::annotate("dtt:requires=_count > 0")]]
void processData(Data& d);

int main() {
    Data d;
    d._count = 10;  // Known: d._count = 10

    printData(d);  // Void + non-const ref = might mutate (heuristic)

    // The call leaves the tracked property unknown.
    processData(d);  // ERROR: printData has no purity contract  dtt-error {{_count > 0}}

    // WORKAROUND: User must add [[clang::annotate("dtt:pure")]] to printData
    // or use [[clang::annotate("dtt:ensures=_count == @old(_count)")]]

    return 0;
}
