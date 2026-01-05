// Example 54: Arithmetic expression constraints — should COMPILE
// Tests: arg0 + arg1 <= _size

struct Buffer {
    int* data;
    int _size;
    
    [[clang::annotate("dtt:ensures=_size == arg0")]]
    Buffer(int n) : data(new int[n]), _size(n) {}
    
    // Copy range: start + len must be <= _size
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copyRange(int start, int len) {
        // Would copy data[start..start+len]
    }
    
    // Simple single-arg version
    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int i) { return data[i]; }
};

int main() {
    Buffer buf(10);        // _size = 10
    buf.copyRange(0, 5);   // 0 + 5 = 5 <= 10 ✓
    buf.copyRange(3, 7);   // 3 + 7 = 10 <= 10 ✓
    buf.copyRange(5, 3);   // 5 + 3 = 8 <= 10 ✓
    buf.at(9);             // 9 < 10 ✓
    return 0;
}
