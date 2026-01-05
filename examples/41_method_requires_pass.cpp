// Example 41: Method precondition — should COMPILE

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void remove() { _size--; }
};

int main() {
    Container c;
    c._size = 5;
    
    c.remove();  // OK: _size = 5 > 0
    c.remove();  // OK: _size = 4 > 0
    
    return 0;
}
