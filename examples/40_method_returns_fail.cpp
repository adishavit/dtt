// Example 40: Method returns property — should FAIL

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:returns=_size")]]
    int size() const { return _size; }
    
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void add() { _size++; }
};

[[clang::annotate("dtt:requires=_size >= 5")]]
void needsFive(Container& c);

int main() {
    Container c;
    c._size = 2;
    c.add();  // _size = 3
    
    needsFive(c);  // ERROR: _size = 3, needs >= 5
    
    return 0;
}
