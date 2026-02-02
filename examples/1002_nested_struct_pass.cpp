// Example 1002: Nested struct containing linear resource - proper cleanup
// Constraint tracked through struct member access

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

// Nested resource properly closed
void nested_closed() {
    Connection conn;
    conn.file = my_fopen("data.txt", "r");
    my_fclose(conn.file);
}  // OK: conn.file.@open == 0

int main() {
    nested_closed();
    return 0;
}
