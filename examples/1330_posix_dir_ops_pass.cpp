// 1330: Correct usage of DIR* operations with null checking

#include <cstdlib>
#include <dirent.h>

// Variant 1: readdir() after null check
void variant1_readdir_checked() {
    DIR* d = opendir("/tmp");
    if (d) {
        struct dirent* entry = readdir(d);
        (void)entry;
        closedir(d);
    }
}

// Variant 2: seekdir/telldir/rewinddir after null check
void variant2_dir_ops_checked() {
    DIR* d = opendir("/tmp");
    if (d) {
        long pos = telldir(d);
        seekdir(d, pos);
        rewinddir(d);
        closedir(d);
    }
}

int main() { return 0; }
