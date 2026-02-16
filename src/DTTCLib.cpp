// DTTCLib.cpp - C library function annotations implementation for DTT
//
// Annotations are defined declaratively in annotations/libc.dtt and embedded
// here as a raw string literal. The parser in DTTAnnotationFile.cpp converts
// them into the same FreeFunctionAnnotations map that the old hand-written
// C++ registration code produced.

#include "DTTCLib.h"
#include "DTTAnnotationFile.h"
#include "DTTStdLib.h"

namespace dtt {

llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;

// Embedded C library annotations — edit annotations/libc.dtt for the readable source.
static constexpr const char kLibcAnnotations[] = R"DTT(
fn fgetc(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn getc(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn ftell(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn rewind(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fflush(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn feof(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn ferror(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn clearerr(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fileno(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn ftello(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fseek(3) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fseeko(3) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fgetpos(2) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fsetpos(2) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn setvbuf(4) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn setbuf(2) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fprintf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fscanf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn vfprintf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn vfscanf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fwprintf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fwscanf(2..6) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fputc(2) {
  requires arg1._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn putc(2) {
  requires arg1._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fputs(2) {
  requires arg1._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn ungetc(2) {
  requires arg1._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fgets(3) {
  requires arg2._nonnull != 0 "FILE* may be null (fopen can fail)"
}
fn fread(4) {
  requires arg3._nonnull != 0 "FILE* may be null (fopen can fail)"
  requires arg3._readable != 0 "FILE* not opened for reading"
}
fn fwrite(4) {
  requires arg3._nonnull != 0 "FILE* may be null (fopen can fail)"
  requires arg3._writable != 0 "FILE* not opened for writing"
}
fn fopen(2) {
  ensures result.@open = 1
  scope_exit result.@open == 0
}
fn freopen(3) {
  ensures result.@open = 1
}
fn tmpfile(0) {
  ensures result.@open = 1
  scope_exit result.@open == 0
}
fn fdopen(2) {
  ensures result.@open = 1
  scope_exit result.@open == 0
}
fn popen(2) {
  ensures result.@open = 1
  scope_exit result.@open == 0
}
fn fclose(1) {
  requires arg0._nonnull != 0 "FILE* may be null or already closed"
  ensures arg0.@open = 0
  ensures arg0._nonnull = 0
}
fn pclose(1) {
  requires arg0._nonnull != 0 "FILE* may be null or already closed"
  ensures arg0.@open = 0
  ensures arg0._nonnull = 0
}
fn malloc(1) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn calloc(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn realloc(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn aligned_alloc(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn strdup(1) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn strndup(2) {
  ensures result.@allocated = 1
  scope_exit result.@allocated == 0
}
fn free(1) {
  ensures arg0.@allocated = 0
}
fn open(2) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn open(3) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn openat(3) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn openat(4) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn creat(2) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn dup(1) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn dup2(2) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn dup3(3) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn pipe(1) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn socket(3) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn accept(3) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn accept4(4) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn mkstemp(1) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn epoll_create(1) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn epoll_create1(1) {
  ensures result.@fd = 1
  scope_exit result.@fd == 0
}
fn close(1) {
  ensures arg0.@fd = 0
}
fn opendir(1) {
  ensures result.@dir = 1
  scope_exit result.@dir == 0
}
fn fdopendir(1) {
  ensures result.@dir = 1
  scope_exit result.@dir == 0
}
fn closedir(1) {
  ensures arg0.@dir = 0
}
fn readdir(1) {
  requires arg0._nonnull != 0 "DIR* may be null (opendir can fail)"
}
fn telldir(1) {
  requires arg0._nonnull != 0 "DIR* may be null (opendir can fail)"
}
fn rewinddir(1) {
  requires arg0._nonnull != 0 "DIR* may be null (opendir can fail)"
}
fn seekdir(2) {
  requires arg0._nonnull != 0 "DIR* may be null (opendir can fail)"
}
fn dlopen(2) {
  ensures result.@dl = 1
  scope_exit result.@dl == 0
}
fn dlclose(1) {
  ensures arg0.@dl = 0
}
fn mmap(6) {
  ensures result.@mapped = 1
  scope_exit result.@mapped == 0
}
fn munmap(2) {
  ensures arg0.@mapped = 0
}
fn sem_open(2) {
  ensures result.@sem = 1
  scope_exit result.@sem == 0
}
fn sem_open(4) {
  ensures result.@sem = 1
  scope_exit result.@sem == 0
}
fn sem_close(1) {
  ensures arg0.@sem = 0
}
fn mq_open(2) {
  ensures result.@mq = 1
  scope_exit result.@mq == 0
}
fn mq_open(4) {
  ensures result.@mq = 1
  scope_exit result.@mq == 0
}
fn mq_close(1) {
  ensures arg0.@mq = 0
}
fn shm_open(3) {
  ensures result.@shm = 1
  scope_exit result.@shm == 0
}
fn iconv_open(2) {
  ensures result.@iconv = 1
  scope_exit result.@iconv == 0
}
fn iconv_close(1) {
  ensures arg0.@iconv = 0
}
fn regcomp(3) {
  ensures arg0.@regex = 1
}
fn regfree(1) {
  ensures arg0.@regex = 0
}
fn getaddrinfo(4) {
  ensures arg3.@addrinfo = 1
}
fn freeaddrinfo(1) {
  ensures arg0.@addrinfo = 0
}
fn pthread_mutex_init(2) {
  ensures arg0.@mutex = 1
}
fn pthread_mutex_destroy(1) {
  ensures arg0.@mutex = 0
}
fn pthread_cond_init(2) {
  ensures arg0.@cond = 1
}
fn pthread_cond_destroy(1) {
  ensures arg0.@cond = 0
}
fn pthread_rwlock_init(2) {
  ensures arg0.@rwlock = 1
}
fn pthread_rwlock_destroy(1) {
  ensures arg0.@rwlock = 0
}
)DTT";

void registerCLibAnnotations() {
    // Pass a dummy MethodAnnotations since libc.dtt has no type blocks
    llvm::DenseMap<MethodKey, MethodInfo> unusedMethods;
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> unusedTemplates;
    parseAnnotations(kLibcAnnotations, unusedMethods, FreeFunctionAnnotations,
                     unusedTemplates);
}

} // namespace dtt
