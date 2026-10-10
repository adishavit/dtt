// Example 999b: Ownership transfer via return - caller leaks
// When caller receives a resource, they inherit the ~requires constraint

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Factory function with ~requires on return
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* open_config() {
    return my_fopen("config.txt", "r");
}  // OK: returning transfers to caller

// Caller receives resource but doesn't close - LEAK
void caller_leaks() {
    FILE_handle* cfg = open_config();  // dtt-error {{scope-exit constraint}}
    // Not closed!
}  // Resource leak is diagnosed at the acquisition above.

int main() {
    caller_leaks();
    return 0;
}
