// 1620: Generic use-after-free on pointer passed to ANY function
// DTT should catch freed pointers passed to functions universally,
// not just for individually annotated functions.

#include <cstdlib>
#include <unistd.h>

// Variant 1: freed pointer passed to setenv
void variant1_setenv_uaf() {
    char *name = (char*)malloc(16);
    free(name);
    setenv(name, "val", 1);  // ERROR: use-after-free  dtt-error {{use-after-free}}
}

// Variant 2: freed pointer passed to putenv
void variant2_putenv_uaf() {
    char *str = (char*)malloc(32);
    free(str);
    putenv(str);  // ERROR: use-after-free  dtt-error {{use-after-free}}
}

// Variant 3: freed pointer as second arg
void variant3_setenv_second_arg_uaf() {
    char *value = (char*)malloc(16);
    free(value);
    setenv("HOME", value, 1);  // ERROR: use-after-free  dtt-error {{use-after-free}}
}

int main() { return 0; }
