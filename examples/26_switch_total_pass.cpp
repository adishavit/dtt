// Example 26: Total switch — should COMPILE

enum Color { Red, Green, Blue };

[[clang::annotate("dtt:total")]]
int colorCode(Color c) {
    switch (c) {
        case Red:   return 0;
        case Green: return 1;
        case Blue:  return 2;
    }
    return -1;  // Unreachable if total
}

int main() {
    return colorCode(Red);
}
