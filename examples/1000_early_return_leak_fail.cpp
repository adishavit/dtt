// Example 1000: Early return leak detection
// Resource not closed before early return

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Early return without close - LEAK
// The return statement triggers constraint check, f.@open is still 1
void early_return_leak(bool error) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (error) {
        return;  // ERROR: f.@open == 1, needs @open == 0  dtt-error {{scope-exit constraint}}
    }
    my_fclose(f);
}

int main() {
    return 0;
}
