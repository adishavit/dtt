// Example 124: Contract propagation — this pointer in methods — should COMPILE
// Verifies that 'this' is correctly handled for method body analysis

struct Stack {
    int _size = 0;
    int _data[100];

    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:requires=_size <= 100")]]  // Bounds for _data[100]
    int top() const {
        // 'this->_size > 0 && _size <= 100' is assumed here
        return _data[_size - 1];  // Safe: 0 <= _size-1 < 100
    }

    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:requires=_size <= 100")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    int pop() {
        // We can call top() because we also have _size > 0 && _size <= 100
        int val = top();  // Should PASS: same precondition
        _size--;
        return val;
    }

    [[clang::annotate("dtt:requires=_size >= 2")]]
    [[clang::annotate("dtt:requires=_size <= 100")]]
    int topTwo() {
        // _size >= 2 implies _size > 0
        return top() + top();  // Should PASS
    }
};

int main() { return 0; }
