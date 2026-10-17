// A declared inequality postcondition on the receiver bounds the field for the
// subsequent precondition check.

struct Builder {
    int* data;
    int _size;
    int _capacity;

    // Returns *this after reserving space
    // Postcondition: _capacity >= n
    [[clang::annotate("dtt:ensures=_capacity >= arg0")]]
    Builder& reserve(int n);
};

[[clang::annotate("dtt:requires=_capacity >= 100")]]
void needsCapacity(Builder& b);

int main() {
    Builder b{};

    // reserve(100) has postcondition: _capacity >= 100
    b.reserve(100);

    // Known false positive: the inequality postcondition is not established.
    needsCapacity(b);  // Should PASS: reserve guarantees _capacity >= 100

    return 0;
}
