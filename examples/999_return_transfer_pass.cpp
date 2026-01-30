// Example 999: Ownership transfer via return
// When a function returns a resource, the ~requires constraint transfers to caller

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Factory function - returns open resource
// The ~requires constraint should transfer to caller, not error here
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* open_config() {
    return my_fopen("config.txt", "r");
}  // OK: returning transfers ownership to caller

// Caller receives the constraint
void variant1_caller_closes() {
    FILE_handle* cfg = open_config();
    my_fclose(cfg);
}  // OK: caller closed the resource

// Chained returns - constraint propagates up
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* open_user_config() {
    return open_config();  // Chain the return
}  // OK: returning transfers ownership

void variant2_chained_caller() {
    FILE_handle* cfg = open_user_config();
    my_fclose(cfg);
}  // OK: ultimate caller closed

int main() {
    variant1_caller_closes();
    variant2_chained_caller();
    return 0;
}
