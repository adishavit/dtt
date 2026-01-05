// Example 44: Method boolean postcondition — should COMPILE

struct Container {
    int _size = 0;
    
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const { return _size == 0; }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    int first() const { return 42; }
};

int main() {
    Container c;
    // c._size is unknown
    
    if (!c.empty()) {
        c.first();  // OK: !empty() proves _size > 0
    }
    
    return 0;
}
