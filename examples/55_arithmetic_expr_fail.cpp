// Example 55: Arithmetic expression constraints fail — should FAIL

struct Buffer {
    int* data;
    int _size;
    
    [[clang::annotate("dtt:ensures=_size == arg0")]]
    Buffer(int n) : data(new int[n]), _size(n) {}
    
    // Copy range: start + len must be <= _size
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copyRange(int start, int len) {}
    
    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int i) { return data[i]; }
};

int main() {
    Buffer buf(10);        // _size = 10
    buf.copyRange(5, 6);   // 5 + 6 = 11 > 10 ERROR  dtt-error {{arg0 \+ arg1 <= _size}}
    return 0;
}
