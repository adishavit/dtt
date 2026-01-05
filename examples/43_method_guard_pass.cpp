// Example 43: Method call in guard — should COMPILE

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:returns=_size")]]
    int size() const { return _size; }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    int first() const { return 42; }
};

int main() {
    Container c;
    // c._size is unknown
    
    if (c.size() > 0) {
        c.first();  // OK: guarded by size() > 0
    }
    
    return 0;
}
