#include <cstdio>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

void example() {
#if CASE == 0
    fflush(nullptr);
#elif CASE == 1
    FILE* all_streams = nullptr;
    fflush(all_streams);
#elif CASE == 2
    FILE* f = tmpfile();
    if (f) { fflush(f); fclose(f); }
#elif CASE == 3
    int fd = shm_open("/dtt-test", O_RDWR, 0600);
    if (fd >= 0) close(fd);
#elif CASE == 4
    int fd = shm_open("/dtt-test", O_RDWR, 0600);
    if (fd < 0) return;
    close(fd);
#elif CASE == 5
    int fd = shm_open("/dtt-test", O_RDWR, 0600);
    if (fd < 0) return;
    // Must diagnose a confirmed acquired descriptor left open.
#elif CASE == 6
    fgetc(nullptr); // Other FILE* APIs must retain their non-null requirement.
#elif CASE == 7
    int descriptors[2];
    int status = pipe(descriptors);
    if (status == 0) { close(descriptors[0]); close(descriptors[1]); }
#elif CASE == 8
    int descriptors[2];
    int status = pipe(descriptors);
    if (status < 0) return;
    close(descriptors[0]); close(descriptors[1]);
#endif
}
