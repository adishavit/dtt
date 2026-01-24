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
}

} // namespace dtt
