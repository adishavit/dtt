// Example 27: Non-total switch — should FAIL

enum Color { Red, Green, Blue };

[[clang::annotate("dtt:total")]]
int colorCode(Color c) {
    switch (c) {
        case Red:   return 0;
        case Green: return 1;
        // Missing: Blue!
    }
    return -1;
}

int main() {
    return colorCode(Red);
}
