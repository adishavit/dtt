// Example 39: Method returns property — should COMPILE

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:returns=_size")]]
    int size() const { return _size; }
    
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void add() { _size++; }
};

[[clang::annotate("dtt:requires=_size >= 3")]]
void needsThree(Container& c);

int main() {
    Container c;
    c._size = 2;
    c.add();  // _size = 3
    
    needsThree(c);  // OK: _size = 3
    
    return 0;
}
