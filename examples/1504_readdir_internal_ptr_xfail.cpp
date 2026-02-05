// 1504: readdir() returns pointer to internal static buffer - DTT gap
// readdir returns a pointer to an internal dirent struct that is
// invalidated by the next readdir() call or by closedir().
// DTT cannot track "pointer-to-internal-buffer" invalidation semantics.

#include <cstdlib>
#include <dirent.h>
#include <cstring>

// Variant 1: Previous readdir result invalidated by next call
void variant1_readdir_overwritten() {
    DIR* d = opendir("/tmp");
    if (!d) return;

    struct dirent* entry1 = readdir(d);
    if (!entry1) { closedir(d); return; }

    // Save first entry's d_name pointer for later comparison
    const char* name1_ptr = entry1->d_name;

    struct dirent* entry2 = readdir(d);  // This may overwrite *entry1
    (void)entry2;

    // entry1 may have been overwritten by readdir's internal buffer reuse
    char c = name1_ptr[0];  // BUG: name1_ptr may point to stale/overwritten data
    (void)c;

    closedir(d);
}

// Variant 2: readdir result used after closedir
void variant2_readdir_after_closedir() {
    DIR* d = opendir("/tmp");
    if (!d) return;

    struct dirent* entry = readdir(d);
    closedir(d);

    if (entry) {
        // entry points to internal buffer freed by closedir
        char c = entry->d_name[0];  // BUG: use after closedir
        (void)c;
    }
}

int main() { return 0; }
