// DTTCLib.h - C library function annotations for DTT
//
// Provides semantic annotations for C standard library functions:
// - File I/O: fopen, fread, fwrite, fclose, etc. (FILE* null tracking)
// - Memory: malloc, calloc, realloc, free (pointer null tracking)
// - String: strcpy, strcat, etc. (buffer safety - future)

#ifndef DTT_CLIB_H
#define DTT_CLIB_H

#include "DTTTypes.h"

namespace dtt {

// Global map of free function annotations
extern llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;

// Register all C library function annotations
void registerCLibAnnotations();

} // namespace dtt

#endif // DTT_CLIB_H
