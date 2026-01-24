// 978_file_read_after_close_fail.cpp
// Tests: File resource tracking - read after close caught
// From BLOG.md: File Resources example

struct File {
    int _open;
    int _readable;

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:requires=_readable == 1")]]
    void read(char* buf, int len) {}

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:ensures=_open == 0")]]
    void close() {}
};

void test_read_after_close() {
    File f;
    f._open = 1;
    f._readable = 1;

    f.close();           // _open = 0

    char buf[100];
    f.read(buf, 100);    // ERROR: _open is 0, requires 1
}

int main() {
    test_read_after_close();
    return 0;
}
