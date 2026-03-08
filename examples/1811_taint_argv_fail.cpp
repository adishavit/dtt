// RUN: %clang++ -fplugin=%plugin -std=c++20 -fsyntax-only %s
// EXPECTED: taint sink errors for argv-derived values
// Tests that main(argc, argv) parameters are treated as tainted.

#include <cstdlib>
#include <cstdio>

int main(int argc, char *argv[]) {
    // Variant 1: direct argv use in system() - command injection
    if (argc > 1 && argv[1]) {
        system(argv[1]); // ERROR: tainted shell command (CWE-78)
    }

    // Variant 2: argv used as file path
    if (argc > 2 && argv[2]) {
        FILE *f = fopen(argv[2], "r"); // ERROR: tainted file path (CWE-22)
        if (f) fclose(f);
    }

    // Variant 3: atoi(argv[1]) used as allocation size
    if (argc > 1 && argv[1]) {
        int n = atoi(argv[1]); // n is tainted (argv -> atoi propagation)
        char *buf = (char *)malloc(n); // ERROR: tainted allocation size (CWE-190)
        if (buf) free(buf);
    }

    return 0;
}
