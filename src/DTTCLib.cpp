// DTTCLib.cpp - C library function annotations implementation for DTT

#include "DTTCLib.h"

namespace dtt {

llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;

void registerCLibAnnotations() {
    // =========================================================================
    // Helpers to register FILE* function preconditions
    // =========================================================================

    // Add _nonnull precondition for FILE* argument
    auto addFilePreNonnull = [](const char* fn, unsigned arity, int fileArgIdx) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            fileArgIdx, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null (fopen can fail)"
        });
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Add _nonnull AND _readable preconditions for read functions
    auto addFilePreRead = [](const char* fn, unsigned arity, int fileArgIdx) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            fileArgIdx, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null (fopen can fail)"
        });
        info.preconditions.push_back({
            fileArgIdx, "_readable", 0, ProofOp::NE,
            "FILE* not opened for reading"
        });
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Add _nonnull AND _writable preconditions for write functions
    auto addFilePreWrite = [](const char* fn, unsigned arity, int fileArgIdx) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            fileArgIdx, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null (fopen can fail)"
        });
        info.preconditions.push_back({
            fileArgIdx, "_writable", 0, ProofOp::NE,
            "FILE* not opened for writing"
        });
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // =========================================================================
    // File I/O functions - FILE* null tracking
    // =========================================================================

    // fopen/freopen: Return potentially-null FILE*, no precondition to check
    // (The caller must check the return value before using it)
    // We don't register these - unknown pointers default to unknown _nonnull

    // fclose: Handled in resource tracking section below (with @open check)

    // Functions with FILE* as first argument (arg index 0), 1 argument total
    for (const char* fn : {"fgetc", "getc", "ftell", "rewind",
                           "fflush", "feof", "ferror", "clearerr", "fileno",
                           "ftello"}) {
        addFilePreNonnull(fn, 1, 0);
    }

    // fseek(FILE*, long, int) - 3 args, FILE* is arg 0
    addFilePreNonnull("fseek", 3, 0);
    // fseeko(FILE*, off_t, int) - 3 args, FILE* is arg 0
    addFilePreNonnull("fseeko", 3, 0);
    // fgetpos(FILE*, fpos_t*) - 2 args, FILE* is arg 0
    addFilePreNonnull("fgetpos", 2, 0);
    // fsetpos(FILE*, const fpos_t*) - 2 args, FILE* is arg 0
    addFilePreNonnull("fsetpos", 2, 0);
    // setvbuf(FILE*, char*, int, size_t) - 4 args, FILE* is arg 0
    addFilePreNonnull("setvbuf", 4, 0);
    // setbuf(FILE*, char*) - 2 args, FILE* is arg 0
    addFilePreNonnull("setbuf", 2, 0);

    // fprintf, fscanf, vfprintf, vfscanf, fwprintf, fwscanf - FILE* is first arg, variable arity
    // Register common arities (2-6 args)
    for (const char* fn : {"fprintf", "fscanf", "vfprintf", "vfscanf",
                           "fwprintf", "fwscanf"}) {
        for (unsigned arity = 2; arity <= 6; ++arity) {
            addFilePreNonnull(fn, arity, 0);
        }
    }

    // Functions with FILE* as second argument (arg index 1)
    for (const char* fn : {"fputc", "putc", "fputs", "ungetc"}) {
        addFilePreNonnull(fn, 2, 1);
    }

    // fgets: FILE* is third argument (arg index 2)
    addFilePreNonnull("fgets", 3, 2);

    // fread: FILE* is fourth argument, requires _readable
    addFilePreRead("fread", 4, 3);
    // fwrite: FILE* is fourth argument, requires _writable
    addFilePreWrite("fwrite", 4, 3);

    // =========================================================================
    // RESOURCE TRACKING - Scope-exit constraints for linear type semantics
    // =========================================================================

    // result.@field = 1, scope-exit requires result.@field == 0
    auto addResourceAcquire = [](const char* fn, unsigned arity,
                                  const char* field) {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({field, 1});
        info.scopeExitRequires.push_back({field, ProofOp::EQ, 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // arg0.@field = 0 (marks resource as released)
    auto addResourceRelease = [](const char* fn, unsigned arity,
                                  const char* field) {
        FreeFunctionInfo info;
        info.postconditions.push_back({0, field, 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // -------------------------------------------------------------------------
    // FILE I/O - fopen/fclose family (@open ghost field)
    // -------------------------------------------------------------------------
    // fopen: FILE* fopen(const char* path, const char* mode)
    // Returns @open=1, requires @open==0 at scope exit
    addResourceAcquire("fopen", 2, "@open");

    // freopen: FILE* freopen(const char* path, const char* mode, FILE* stream)
    // Closes arg2 internally, returns new stream (or NULL on failure).
    // Common pattern: fp = freopen(..., fp) - result assigned back to same var.
    // Only track result.@open=1; arg2 closing is implicit (same var gets overwritten).
    // No scope-exit requirement - inherits from original fopen's constraint.
    {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({"@open", 1});
        FreeFunctionAnnotations[{"freopen", 3}] = info;
    }

    // tmpfile: FILE* tmpfile(void)
    addResourceAcquire("tmpfile", 0, "@open");

    // fdopen (POSIX): FILE* fdopen(int fd, const char* mode)
    addResourceAcquire("fdopen", 2, "@open");

    // popen (POSIX): FILE* popen(const char* command, const char* type)
    addResourceAcquire("popen", 2, "@open");

    // fclose: int fclose(FILE* stream)
    // Requires _nonnull (catches double-close via invalidation), sets @open=0
    {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            0, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null or already closed"
        });
        info.postconditions.push_back({0, "@open", 0});
        info.postconditions.push_back({0, "_nonnull", 0});  // Invalidate for double-close
        FreeFunctionAnnotations[{"fclose", 1}] = info;
    }

    // pclose (POSIX): int pclose(FILE* stream)
    {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            0, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null or already closed"
        });
        info.postconditions.push_back({0, "@open", 0});
        info.postconditions.push_back({0, "_nonnull", 0});  // Invalidate for double-close
        FreeFunctionAnnotations[{"pclose", 1}] = info;
    }

    // -------------------------------------------------------------------------
    // MEMORY ALLOCATION - malloc/free family (@allocated ghost field)
    // -------------------------------------------------------------------------
    // malloc: void* malloc(size_t size)
    addResourceAcquire("malloc", 1, "@allocated");

    // calloc: void* calloc(size_t nmemb, size_t size)
    addResourceAcquire("calloc", 2, "@allocated");

    // realloc: void* realloc(void* ptr, size_t size)
    // Note: realloc is special - can return new allocation
    addResourceAcquire("realloc", 2, "@allocated");

    // aligned_alloc: void* aligned_alloc(size_t alignment, size_t size)
    addResourceAcquire("aligned_alloc", 2, "@allocated");

    // strdup (POSIX): char* strdup(const char* s)
    addResourceAcquire("strdup", 1, "@allocated");

    // strndup (POSIX): char* strndup(const char* s, size_t n)
    addResourceAcquire("strndup", 2, "@allocated");

    // free: void free(void* ptr)
    // Requires @allocated==1, sets @allocated=0
    addResourceRelease("free", 1, "@allocated");

    // -------------------------------------------------------------------------
    // POSIX FILE DESCRIPTORS - open/close family (@fd ghost field)
    // -------------------------------------------------------------------------
    // open: int open(const char* pathname, int flags)
    // open: int open(const char* pathname, int flags, mode_t mode)
    {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({"@fd", 1});
        info.scopeExitRequires.push_back({"@fd", ProofOp::EQ, 0});
        FreeFunctionAnnotations[{"open", 2}] = info;
        FreeFunctionAnnotations[{"open", 3}] = info;
    }

    // openat: int openat(int dirfd, const char* pathname, int flags)
    // openat: int openat(int dirfd, const char* pathname, int flags, mode_t mode)
    {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({"@fd", 1});
        info.scopeExitRequires.push_back({"@fd", ProofOp::EQ, 0});
        FreeFunctionAnnotations[{"openat", 3}] = info;
        FreeFunctionAnnotations[{"openat", 4}] = info;
    }

    // creat: int creat(const char* pathname, mode_t mode)
    addResourceAcquire("creat", 2, "@fd");

    // dup: int dup(int oldfd)
    addResourceAcquire("dup", 1, "@fd");

    // dup2: int dup2(int oldfd, int newfd)
    addResourceAcquire("dup2", 2, "@fd");

    // dup3: int dup3(int oldfd, int newfd, int flags)
    addResourceAcquire("dup3", 3, "@fd");

    // pipe: int pipe(int pipefd[2]) - special: creates 2 fds
    // Note: simplified - just track the call happened
    addResourceAcquire("pipe", 1, "@fd");

    // socket: int socket(int domain, int type, int protocol)
    addResourceAcquire("socket", 3, "@fd");

    // accept: int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
    addResourceAcquire("accept", 3, "@fd");

    // accept4: int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
    addResourceAcquire("accept4", 4, "@fd");

    // mkstemp: int mkstemp(char* template) - creates temp file, returns fd
    addResourceAcquire("mkstemp", 1, "@fd");

    // epoll_create: int epoll_create(int size) - returns epoll fd
    addResourceAcquire("epoll_create", 1, "@fd");

    // epoll_create1: int epoll_create1(int flags) - returns epoll fd
    addResourceAcquire("epoll_create1", 1, "@fd");

    // close: int close(int fd)
    addResourceRelease("close", 1, "@fd");

    // -------------------------------------------------------------------------
    // DIRECTORY STREAMS - opendir/closedir family (@dir ghost field)
    // -------------------------------------------------------------------------
    // opendir: DIR* opendir(const char* name)
    addResourceAcquire("opendir", 1, "@dir");

    // fdopendir: DIR* fdopendir(int fd)
    addResourceAcquire("fdopendir", 1, "@dir");

    // closedir: int closedir(DIR* dirp)
    addResourceRelease("closedir", 1, "@dir");

    // -------------------------------------------------------------------------
    // DYNAMIC LIBRARIES - dlopen/dlclose family (@dl ghost field)
    // -------------------------------------------------------------------------
    // dlopen: void* dlopen(const char* filename, int flags)
    addResourceAcquire("dlopen", 2, "@dl");

    // dlclose: int dlclose(void* handle)
    addResourceRelease("dlclose", 1, "@dl");

    // -------------------------------------------------------------------------
    // MEMORY MAPPING - mmap/munmap family (@mapped ghost field)
    // -------------------------------------------------------------------------
    // mmap: void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
    addResourceAcquire("mmap", 6, "@mapped");

    // munmap: int munmap(void* addr, size_t length)
    addResourceRelease("munmap", 2, "@mapped");

    // -------------------------------------------------------------------------
    // POSIX SEMAPHORES - sem_open/sem_close family (@sem ghost field)
    // -------------------------------------------------------------------------
    // sem_open: sem_t* sem_open(const char* name, int oflag)
    // sem_open: sem_t* sem_open(const char* name, int oflag, mode_t mode, unsigned int value)
    {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({"@sem", 1});
        info.scopeExitRequires.push_back({"@sem", ProofOp::EQ, 0});
        FreeFunctionAnnotations[{"sem_open", 2}] = info;
        FreeFunctionAnnotations[{"sem_open", 4}] = info;
    }

    // sem_close: int sem_close(sem_t* sem)
    addResourceRelease("sem_close", 1, "@sem");

    // -------------------------------------------------------------------------
    // POSIX MESSAGE QUEUES - mq_open/mq_close family (@mq ghost field)
    // -------------------------------------------------------------------------
    // mq_open: mqd_t mq_open(const char* name, int oflag)
    // mq_open: mqd_t mq_open(const char* name, int oflag, mode_t mode, struct mq_attr* attr)
    {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({"@mq", 1});
        info.scopeExitRequires.push_back({"@mq", ProofOp::EQ, 0});
        FreeFunctionAnnotations[{"mq_open", 2}] = info;
        FreeFunctionAnnotations[{"mq_open", 4}] = info;
    }

    // mq_close: int mq_close(mqd_t mqdes)
    addResourceRelease("mq_close", 1, "@mq");

    // -------------------------------------------------------------------------
    // POSIX SHARED MEMORY - shm_open/shm_unlink family (@shm ghost field)
    // -------------------------------------------------------------------------
    // shm_open: int shm_open(const char* name, int oflag, mode_t mode)
    addResourceAcquire("shm_open", 3, "@shm");

    // Note: shm_unlink removes the name, close(fd) closes the descriptor

    // -------------------------------------------------------------------------
    // ICONV - iconv_open/iconv_close family (@iconv ghost field)
    // -------------------------------------------------------------------------
    // iconv_open: iconv_t iconv_open(const char* tocode, const char* fromcode)
    addResourceAcquire("iconv_open", 2, "@iconv");

    // iconv_close: int iconv_close(iconv_t cd)
    addResourceRelease("iconv_close", 1, "@iconv");

    // -------------------------------------------------------------------------
    // REGEX - regcomp/regfree family (@regex ghost field)
    // -------------------------------------------------------------------------
    // Note: regcomp initializes a regex_t, regfree releases it
    // regcomp: int regcomp(regex_t* preg, const char* regex, int cflags)
    {
        FreeFunctionInfo info;
        info.postconditions.push_back({0, "@regex", 1});  // arg0.@regex = 1
        // Note: Can't easily express ~requires on arg here; user should use annotation
        FreeFunctionAnnotations[{"regcomp", 3}] = info;
    }

    // regfree: void regfree(regex_t* preg)
    addResourceRelease("regfree", 1, "@regex");

    // -------------------------------------------------------------------------
    // GETADDRINFO - getaddrinfo/freeaddrinfo family (@addrinfo ghost field)
    // -------------------------------------------------------------------------
    // getaddrinfo: int getaddrinfo(const char* node, const char* service,
    //                              const struct addrinfo* hints, struct addrinfo** res)
    // Note: Sets *res to allocated chain; complex to track precisely
    {
        FreeFunctionInfo info;
        info.postconditions.push_back({3, "@addrinfo", 1});  // arg3 (the **res) gets result
        FreeFunctionAnnotations[{"getaddrinfo", 4}] = info;
    }

    // freeaddrinfo: void freeaddrinfo(struct addrinfo* res)
    addResourceRelease("freeaddrinfo", 1, "@addrinfo");

    // -------------------------------------------------------------------------
    // DIRECTORY STREAM OPERATIONS - null check for DIR* argument
    // -------------------------------------------------------------------------
    // These operate on DIR* which may be null if opendir() failed.
    // readdir: struct dirent* readdir(DIR* dirp)           - 1 arg
    // telldir: long telldir(DIR* dirp)                     - 1 arg
    // rewinddir: void rewinddir(DIR* dirp)                 - 1 arg
    // seekdir: void seekdir(DIR* dirp, long loc)           - 2 args
    struct { const char* fn; unsigned arity; } dirOps[] = {
        {"readdir", 1}, {"telldir", 1}, {"rewinddir", 1}, {"seekdir", 2}
    };
    for (const auto& [fn, arity] : dirOps) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            0, "_nonnull", 0, ProofOp::NE,
            "DIR* may be null (opendir can fail)"
        });
        FreeFunctionAnnotations[{fn, arity}] = info;
    }

    // -------------------------------------------------------------------------
    // PTHREAD SYNCHRONIZATION - init/destroy lifecycle pairs
    // -------------------------------------------------------------------------
    // Helper for out-parameter resource tracking:
    // init sets arg0.@field = 1; destroy sets arg0.@field = 0
    // Note: scope-exit requires on out-params is not yet supported by the
    // FreeFunctionInfo model (it only supports result scope-exit). For now,
    // we track the postcondition so DTT knows the resource state.

    // pthread_mutex_init: int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
    {
        FreeFunctionInfo info;
        info.postconditions.push_back({0, "@mutex", 1});
        FreeFunctionAnnotations[{"pthread_mutex_init", 2}] = info;
    }
    // pthread_mutex_destroy: int pthread_mutex_destroy(pthread_mutex_t* mutex)
    addResourceRelease("pthread_mutex_destroy", 1, "@mutex");

    // pthread_cond_init: int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
    {
        FreeFunctionInfo info;
        info.postconditions.push_back({0, "@cond", 1});
        FreeFunctionAnnotations[{"pthread_cond_init", 2}] = info;
    }
    // pthread_cond_destroy: int pthread_cond_destroy(pthread_cond_t* cond)
    addResourceRelease("pthread_cond_destroy", 1, "@cond");

    // pthread_rwlock_init: int pthread_rwlock_init(pthread_rwlock_t* rwlock, const pthread_rwlockattr_t* attr)
    {
        FreeFunctionInfo info;
        info.postconditions.push_back({0, "@rwlock", 1});
        FreeFunctionAnnotations[{"pthread_rwlock_init", 2}] = info;
    }
    // pthread_rwlock_destroy: int pthread_rwlock_destroy(pthread_rwlock_t* rwlock)
    addResourceRelease("pthread_rwlock_destroy", 1, "@rwlock");
}

} // namespace dtt
