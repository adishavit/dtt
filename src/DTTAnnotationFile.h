// DTTAnnotationFile.h - .dtt annotation file parser for DTT
//
// Parses declarative annotation files (.dtt format) into the same data
// structures used by DTTStdLib.cpp and DTTCLib.cpp. This enables:
// - Replacing hardcoded C++ annotations with readable .dtt files
// - User-defined annotation files via --dtt-annotations= plugin arg
// - Third-party library annotation packages (e.g., openssl.dtt)

#ifndef DTT_ANNOTATION_FILE_H
#define DTT_ANNOTATION_FILE_H

#include "DTTTypes.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace dtt {

/// Parse .dtt format annotation string into existing maps.
/// Returns true on success, false on parse error.
/// Errors are reported to llvm::errs().
bool parseAnnotations(
    llvm::StringRef content,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases);

/// Parse .dtt file from disk.
/// Returns true on success, false on file read or parse error.
bool parseAnnotationFile(
    llvm::StringRef path,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases);

} // namespace dtt

#endif // DTT_ANNOTATION_FILE_H
