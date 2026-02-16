// DTTWinLib.cpp - Windows API function annotations implementation for DTT
//
// Annotations are defined declaratively in annotations/win32.dtt and embedded
// here as a raw string literal.

#include "DTTWinLib.h"
#include "DTTCLib.h"
#include "DTTAnnotationFile.h"
#include "DTTStdLib.h"

namespace dtt {

// Embedded Windows annotations — edit annotations/win32.dtt for the readable source.
static constexpr const char kWin32Annotations[] = R"DTT(
fn CreateFileA(7) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateFileW(7) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn FindFirstFileA(2) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn FindFirstFileW(2) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateEventA(4) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateEventW(4) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateMutexA(3) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateMutexW(3) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateSemaphoreA(4) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateSemaphoreW(4) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateFileMappingA(6) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CreateFileMappingW(6) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn CloseHandle(1) {
  requires arg0._nonnull != 0 "HANDLE may be invalid or already closed"
  ensures arg0.@handle = 0
  ensures arg0._nonnull = 0
}
fn FindClose(1) {
  requires arg0._nonnull != 0 "HANDLE may be invalid or already closed"
  ensures arg0.@handle = 0
  ensures arg0._nonnull = 0
}
fn FindNextFileA(2) {
  requires arg0._nonnull != 0 "HANDLE may be invalid or already closed"
}
fn FindNextFileW(2) {
  requires arg0._nonnull != 0 "HANDLE may be invalid or already closed"
}
fn CreateCompatibleDC(1) {
  ensures result.@handle = 1
  scope_exit result.@handle == 0
}
fn DeleteDC(1) {
  requires arg0._nonnull != 0 "HDC may be invalid or already deleted"
  ensures arg0.@handle = 0
  ensures arg0._nonnull = 0
}
fn DeleteObject(1) {
  requires arg0._nonnull != 0 "GDI object may be invalid or already deleted"
  ensures arg0.@handle = 0
  ensures arg0._nonnull = 0
}
fn HeapCreate(3) {
  ensures result.@heap = 1
  scope_exit result.@heap == 0
}
fn HeapDestroy(1) {
  requires arg0._nonnull != 0 "Heap HANDLE may be invalid or already destroyed"
  ensures arg0.@heap = 0
  ensures arg0._nonnull = 0
}
fn HeapAlloc(3) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn HeapFree(3) {
  ensures arg2.@allocated = 0
}
fn MapViewOfFile(5) {
  ensures result.@mapped = 1
  scope_exit result.@mapped == 0
}
fn UnmapViewOfFile(1) {
  requires arg0._nonnull != 0 "View may be invalid or already unmapped"
  ensures arg0.@mapped = 0
  ensures arg0._nonnull = 0
}
fn SysAllocString(1) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn SysAllocStringLen(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn SysAllocStringByteLen(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn SysFreeString(1) {
  ensures arg0.@allocated = 0
}
fn CoTaskMemAlloc(1) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn CoTaskMemFree(1) {
  ensures arg0.@allocated = 0
}
fn LocalAlloc(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn LocalFree(1) {
  ensures arg0.@allocated = 0
}
fn GlobalAlloc(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn GlobalFree(1) {
  ensures arg0.@allocated = 0
}
fn SafeArrayCreate(3) {
  ensures result.@safearray = 1
  scope_exit result.@safearray == 0
}
fn SafeArrayCreateVector(3) {
  ensures result.@safearray = 1
  scope_exit result.@safearray == 0
}
fn SafeArrayDestroy(1) {
  requires arg0._nonnull != 0 "SAFEARRAY may be invalid or already destroyed"
  ensures arg0.@safearray = 0
  ensures arg0._nonnull = 0
}
)DTT";

void registerWindowsAnnotations() {
    llvm::DenseMap<MethodKey, MethodInfo> unusedMethods;
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> unusedTemplates;
    parseAnnotations(kWin32Annotations, unusedMethods, FreeFunctionAnnotations,
                     unusedTemplates);
}

} // namespace dtt
