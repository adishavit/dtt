// Example 119: Contract propagation — method needs stronger constraint — should FAIL
// Method body tries to call something requiring more

struct Container {
    int _size = 0;

    [[clang::annotate("dtt:requires=_size >= 5")]]
    void needsFive();

    // This method only requires _size > 0, but tries to call needsFive()
    [[clang::annotate("dtt:requires=_size > 0")]]
    void process() {
        needsFive();  // Should FAIL: _size > 0 does NOT imply _size >= 5  dtt-error {{_size >= 5}}
    }
};

int main() { return 0; }
