// DTTCLib.cpp - C library function annotations implementation for DTT

#include "DTTCLib.h"

namespace dtt {

llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;

void registerCLibAnnotations() {
    // =========================================================================
    // Helper to register a FILE* function that requires non-null FILE* argument
    // =========================================================================
    auto addFilePreNonnull = [](const char* fn, unsigned arity, int fileArgIdx,
                                bool invalidatesAfter = false) {
        FreeFunctionInfo info;
        info.argToCheck = fileArgIdx;
        info.fieldToCheck = "_nonnull";
        info.requiredValue = 0;
        info.checkOp = ProofOp::NE;  // _nonnull != 0
        info.errorReason = "FILE* may be null (fopen can fail)";

        if (invalidatesAfter) {
            info.argToModify = fileArgIdx;
            info.fieldToSet = "_nonnull";
            info.valueToSet = 0;  // Mark as invalid after close
        }

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

    // fread, fwrite: FILE* is fourth argument (arg index 3)
    addFilePreNonnull("fread", 4, 3);
    addFilePreNonnull("fwrite", 4, 3);

    // =========================================================================
    // Memory allocation functions (future)
    // =========================================================================
    // malloc, calloc, realloc return potentially-null pointers
    // free requires non-null (or null is a no-op)
    // TODO: Add when needed

    // =========================================================================
    // String functions (future)
    // =========================================================================
    // strcpy, strcat, etc. have buffer safety requirements
    // TODO: Add when needed
}

} // namespace dtt
