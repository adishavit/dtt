// Example 42: Method precondition — should FAIL

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void remove() { _size--; }
};

int main() {
    Container c;
    c._size = 0;
    
    c.remove();  // ERROR: _size = 0, not > 0  dtt-error {{_size > 0}}
    
    return 0;
}
