// Example 1004: Overwrite pointer with constraint - immediate leak
// When a pointer with constraint is overwritten, the resource is leaked

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Overwrite with nullptr - LEAK
void overwrite_nullptr() {
    FILE_handle* f = my_fopen("data.txt", "r");
    f = nullptr;  // ERROR: f.@open == 1, needs close before overwrite  dtt-error {{leaks resource}}
}

// Overwrite with another fopen - LEAK (first file)
void overwrite_new_file() {
    FILE_handle* f = my_fopen("file1.txt", "r");
    f = my_fopen("file2.txt", "r");  // ERROR: first file leaked  dtt-error {{leaks resource}}
    my_fclose(f);  // Only closes second file
}

// Proper close before overwrite - OK
void close_before_overwrite() {
    FILE_handle* f = my_fopen("file1.txt", "r");
    my_fclose(f);  // Close first
    f = my_fopen("file2.txt", "r");  // OK: first file was closed
    my_fclose(f);
}

int main() {
    return 0;
}
