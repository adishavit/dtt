// Example 1002: Nested struct containing linear resource - leak case
// When struct member receives linear resource, constraint is tracked

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

struct Connection {
    FILE_handle* file;
    int id;
};

// Nested resource not closed - LEAK
void nested_leak() {
    Connection conn;
    conn.file = my_fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    conn.id = 42;
}  // Resource leak is diagnosed at the acquisition above.

int main() {
    return 0;
}
