// DTTCLib.cpp - C library function annotations implementation for DTT

#include "DTTCLib.h"

namespace dtt {

llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;

void registerCLibAnnotations() {
    // =========================================================================
    // Helpers to register FILE* function preconditions
    // =========================================================================

    // Add _nonnull precondition for FILE* argument
    auto addFilePreNonnull = [](const char* fn, unsigned arity, int fileArgIdx,
                                bool invalidatesAfter = false) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            fileArgIdx, "_nonnull", 0, ProofOp::NE,
            "FILE* may be null (fopen can fail)"
        });

        if (invalidatesAfter) {
            info.postconditions.push_back({fileArgIdx, "_nonnull", 0});
        }

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

    // fclose: Requires non-null, then invalidates the FILE*
    addFilePreNonnull("fclose", 1, 0, /*invalidatesAfter=*/true);

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

    // fprintf, fscanf, vfprintf, vfscanf - FILE* is first arg, variable arity
    // Register common arities (2-6 args)
    for (const char* fn : {"fprintf", "fscanf", "vfprintf", "vfscanf"}) {
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
    // String/Memory functions - use-after-free detection
    // =========================================================================
    // These functions access memory through pointer arguments.
    // If a freed pointer is passed, it's use-after-free.

    // Lifetime precondition registration table
    // {function_name, arity, {arg indices to check}}
    struct LifetimeEntry {
        const char* fn;
        unsigned arity;
        int args[2];  // Max 2 pointer args; -1 = unused
    };

    static constexpr LifetimeEntry lifetimeChecks[] = {
        // String copy/concatenation - check both dst and src
        {"strcpy", 2, {0, 1}},
        {"strncpy", 3, {0, 1}},
        {"strcat", 2, {0, 1}},
        {"strncat", 3, {0, 1}},
        // String examination
        {"strlen", 1, {0, -1}},
        {"strcmp", 2, {0, 1}},
        {"strncmp", 3, {0, 1}},
        {"strchr", 2, {0, -1}},
        {"strrchr", 2, {0, -1}},
        {"strstr", 2, {0, 1}},
        {"strpbrk", 2, {0, 1}},
        {"strspn", 2, {0, 1}},
        {"strcspn", 2, {0, 1}},
        // Memory operations
        {"memcpy", 3, {0, 1}},
        {"memmove", 3, {0, 1}},
        {"memset", 3, {0, -1}},
        {"memcmp", 3, {0, 1}},
        {"memchr", 3, {0, -1}},
    };

    for (const auto& entry : lifetimeChecks) {
        FreeFunctionInfo info;
        info.preconditions.reserve(2);
        for (int idx : entry.args) {
            if (idx < 0) break;
            info.preconditions.push_back({
                idx, "_lifetime", 0, ProofOp::NE,
                "pointer was freed (use-after-free)", true
            });
        }
        FreeFunctionAnnotations[{entry.fn, entry.arity}] = info;
    }

    // Variable-arity functions (sprintf, snprintf, sscanf)
    // Format: {name, min_arity, max_arity, buffer_arg_index}
    struct VarArityEntry {
        const char* fn;
        unsigned minArity;
        unsigned maxArity;
        int bufArg;
    };

    static constexpr VarArityEntry varArityChecks[] = {
        {"sprintf", 2, 8, 0},
        {"snprintf", 3, 8, 0},
        {"sscanf", 2, 8, 0},
    };

    for (const auto& entry : varArityChecks) {
        for (unsigned arity = entry.minArity; arity <= entry.maxArity; ++arity) {
            FreeFunctionInfo info;
            info.preconditions.push_back({
                entry.bufArg, "_lifetime", 0, ProofOp::NE,
                "pointer was freed (use-after-free)", true
            });
            FreeFunctionAnnotations[{entry.fn, arity}] = info;
        }
    }

    // =========================================================================
    // RESOURCE TRACKING - Scope-exit constraints for linear type semantics
    // =========================================================================
    // These annotations enable compile-time detection of resource leaks.
    // Resource-acquiring functions set a ghost field and require it to be
    // cleared (via the corresponding release function) before scope exit.

    // Helper to register resource-acquiring functions
    // Returns: result.@field = acquireValue, ~requires: result.@field == releaseValue
    auto addResourceAcquire = [](const char* fn, unsigned arity,
                                  const char* field, int64_t acquireValue,
                                  int64_t releaseValue) {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({field, acquireValue});
        info.scopeExitRequires.push_back({field, ProofOp::EQ, releaseValue});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Helper to register resource-releasing functions
    // Ensures: arg.@field = releaseValue (marks resource as released)
    // Note: No precondition check - rely on scope-exit constraints from acquire
    auto addResourceRelease = [](const char* fn, unsigned arity, int argIdx,
                                  const char* field, int64_t /*acquireValue*/,
                                  int64_t releaseValue) {
        FreeFunctionInfo info;
        info.postconditions.push_back({argIdx, field, releaseValue});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // -------------------------------------------------------------------------
    // FILE I/O - fopen/fclose family (@open ghost field)
    // -------------------------------------------------------------------------
    // fopen: FILE* fopen(const char* path, const char* mode)
    // Returns @open=1, requires @open==0 at scope exit
    addResourceAcquire("fopen", 2, "@open", 1, 0);

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
    addResourceAcquire("tmpfile", 0, "@open", 1, 0);

    // fdopen (POSIX): FILE* fdopen(int fd, const char* mode)
    addResourceAcquire("fdopen", 2, "@open", 1, 0);

    // popen (POSIX): FILE* popen(const char* command, const char* type)
    addResourceAcquire("popen", 2, "@open", 1, 0);

    // fclose: int fclose(FILE* stream)
    // Requires @open==1, sets @open=0
    addResourceRelease("fclose", 1, 0, "@open", 1, 0);

    // pclose (POSIX): int pclose(FILE* stream)
    addResourceRelease("pclose", 1, 0, "@open", 1, 0);

    // -------------------------------------------------------------------------
    // MEMORY ALLOCATION - malloc/free family (@allocated ghost field)
    // -------------------------------------------------------------------------
    // malloc: void* malloc(size_t size)
    addResourceAcquire("malloc", 1, "@allocated", 1, 0);

    // calloc: void* calloc(size_t nmemb, size_t size)
    addResourceAcquire("calloc", 2, "@allocated", 1, 0);

    // realloc: void* realloc(void* ptr, size_t size)
    // Note: realloc is special - can return new allocation
    addResourceAcquire("realloc", 2, "@allocated", 1, 0);

    // aligned_alloc: void* aligned_alloc(size_t alignment, size_t size)
    addResourceAcquire("aligned_alloc", 2, "@allocated", 1, 0);

    // strdup (POSIX): char* strdup(const char* s)
    addResourceAcquire("strdup", 1, "@allocated", 1, 0);

    // strndup (POSIX): char* strndup(const char* s, size_t n)
    addResourceAcquire("strndup", 2, "@allocated", 1, 0);

    // free: void free(void* ptr)
    // Requires @allocated==1, sets @allocated=0
    addResourceRelease("free", 1, 0, "@allocated", 1, 0);

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
    addResourceAcquire("creat", 2, "@fd", 1, 0);

    // dup: int dup(int oldfd)
    addResourceAcquire("dup", 1, "@fd", 1, 0);

    // dup2: int dup2(int oldfd, int newfd)
    addResourceAcquire("dup2", 2, "@fd", 1, 0);

    // dup3: int dup3(int oldfd, int newfd, int flags)
    addResourceAcquire("dup3", 3, "@fd", 1, 0);

    // pipe: int pipe(int pipefd[2]) - special: creates 2 fds
    // Note: simplified - just track the call happened
    addResourceAcquire("pipe", 1, "@fd", 1, 0);

    // socket: int socket(int domain, int type, int protocol)
    addResourceAcquire("socket", 3, "@fd", 1, 0);

    // accept: int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
    addResourceAcquire("accept", 3, "@fd", 1, 0);

    // accept4: int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
    addResourceAcquire("accept4", 4, "@fd", 1, 0);

    // close: int close(int fd)
    addResourceRelease("close", 1, 0, "@fd", 1, 0);

    // -------------------------------------------------------------------------
    // DIRECTORY STREAMS - opendir/closedir family (@dir ghost field)
    // -------------------------------------------------------------------------
    // opendir: DIR* opendir(const char* name)
    addResourceAcquire("opendir", 1, "@dir", 1, 0);

    // fdopendir: DIR* fdopendir(int fd)
    addResourceAcquire("fdopendir", 1, "@dir", 1, 0);

    // closedir: int closedir(DIR* dirp)
    addResourceRelease("closedir", 1, 0, "@dir", 1, 0);

    // -------------------------------------------------------------------------
    // DYNAMIC LIBRARIES - dlopen/dlclose family (@dl ghost field)
    // -------------------------------------------------------------------------
    // dlopen: void* dlopen(const char* filename, int flags)
    addResourceAcquire("dlopen", 2, "@dl", 1, 0);

    // dlclose: int dlclose(void* handle)
    addResourceRelease("dlclose", 1, 0, "@dl", 1, 0);

    // -------------------------------------------------------------------------
    // MEMORY MAPPING - mmap/munmap family (@mapped ghost field)
    // -------------------------------------------------------------------------
    // mmap: void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
    addResourceAcquire("mmap", 6, "@mapped", 1, 0);

    // munmap: int munmap(void* addr, size_t length)
    addResourceRelease("munmap", 2, 0, "@mapped", 1, 0);

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
    addResourceRelease("sem_close", 1, 0, "@sem", 1, 0);

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
    addResourceRelease("mq_close", 1, 0, "@mq", 1, 0);

    // -------------------------------------------------------------------------
    // POSIX SHARED MEMORY - shm_open/shm_unlink family (@shm ghost field)
    // -------------------------------------------------------------------------
    // shm_open: int shm_open(const char* name, int oflag, mode_t mode)
    addResourceAcquire("shm_open", 3, "@shm", 1, 0);

    // Note: shm_unlink removes the name, close(fd) closes the descriptor

    // -------------------------------------------------------------------------
    // ICONV - iconv_open/iconv_close family (@iconv ghost field)
    // -------------------------------------------------------------------------
    // iconv_open: iconv_t iconv_open(const char* tocode, const char* fromcode)
    addResourceAcquire("iconv_open", 2, "@iconv", 1, 0);

    // iconv_close: int iconv_close(iconv_t cd)
    addResourceRelease("iconv_close", 1, 0, "@iconv", 1, 0);

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
    addResourceRelease("regfree", 1, 0, "@regex", 1, 0);

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
    addResourceRelease("freeaddrinfo", 1, 0, "@addrinfo", 1, 0);
}

} // namespace dtt
