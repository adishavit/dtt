// 1330: Null pointer dereference on DIR* operations
// Tests that readdir/seekdir/telldir/rewinddir check for null DIR*

#include <cstdlib>
#include <dirent.h>

// Variant 1: readdir() on potentially-null DIR*
void variant1_readdir_null() {
    DIR* d = opendir("/nonexistent_path");
    // opendir can return NULL - no check before use
    struct dirent* entry = readdir(d);  // BUG: d may be null
    (void)entry;
}

// Variant 2: seekdir() on potentially-null DIR*
void variant2_seekdir_null() {
    DIR* d = opendir("/nonexistent_path");
    seekdir(d, 0);  // BUG: d may be null
}

// Variant 3: telldir() on potentially-null DIR*
void variant3_telldir_null() {
    DIR* d = opendir("/nonexistent_path");
    long pos = telldir(d);  // BUG: d may be null
    (void)pos;
}

// Variant 4: rewinddir() on potentially-null DIR*
void variant4_rewinddir_null() {
    DIR* d = opendir("/nonexistent_path");
    rewinddir(d);  // BUG: d may be null
}

int main() { return 0; }
