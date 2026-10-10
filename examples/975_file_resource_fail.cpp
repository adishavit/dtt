// 975_file_resource_fail.cpp
// Tests: File resource tracking - write to read-only file caught
// From BLOG.md: File Resources example

struct File {
    int _open;
    int _readable;
    int _writable;

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:requires=_writable == 1")]]
    void write(const char* data) {}
};

void test_write_readonly() {
    File f;
    f._open = 1;
    f._readable = 1;
    f._writable = 0;     // Read-only!

    f.write("test");     // ERROR: _writable is 0, requires 1  dtt-error {{_writable == 1}}
}

int main() {
    test_write_readonly();
    return 0;
}
