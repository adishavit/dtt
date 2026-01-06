// Example 118: Contract propagation — method precondition — should COMPILE
// Method requires= becomes an assumption inside the method body

struct Container {
    int _size = 0;
    int* _data = nullptr;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() {
        // Inside this method, we know _size > 0
        // So accessing _data[0] is logically safe (DTT tracks _size, not raw pointer)
        return _data[0];
    }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& back() {
        // We can also call front() here because we have _size > 0
        return front();  // Should PASS: we have the same precondition
    }
};

int main() { return 0; }
