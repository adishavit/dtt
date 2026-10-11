// POSIX popen ownership transfer; Windows exposes a different API.
#include <stdio.h>

// Variant 4: popen wrapper
static FILE* run_command(const char* cmd) {
    FILE* p = popen(cmd, "r");
    return p;  // Ownership transferred to caller
}

void variant4_popen_wrapper() {
    FILE* p = run_command("echo ok");
    if (p) pclose(p);
}

int main() { return 0; }
