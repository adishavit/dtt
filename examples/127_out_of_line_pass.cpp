// Example 127: Out-of-line method definition — should COMPILE
// Testing if out-of-line definitions work differently

struct Container {
    int _size = 0;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int front() const;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int wrapper() const;
};

int Container::front() const { return _size; }

int Container::wrapper() const {
    return this->front();  // Should PASS
}

int main() { return 0; }
