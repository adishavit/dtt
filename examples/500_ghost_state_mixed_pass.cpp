// Example 500: Ghost state mixed _ prefix (runtime field) and @ prefix (ghost state)

struct Buffer {
    int _size = 0;  // Runtime field

    [[clang::annotate("dtt:ensures=_size == arg0"),
      clang::annotate("dtt:ensures=@validated == 1")]]
    void allocate(int size) {}

    [[clang::annotate("dtt:requires=_size > 0"),
      clang::annotate("dtt:requires=@validated == 1")]]
    void write(const char* data) {}
};

int main() {
    Buffer buf;
    buf.allocate(100);  // _size = 100, @validated = 1
    buf.write("test");  // OK: _size > 0, @validated == 1
}
