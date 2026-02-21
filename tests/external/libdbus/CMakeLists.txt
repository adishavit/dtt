# libdbus: FetchContent from freedesktop.org + generate dbus-arch-deps.h
# libdbus uses Meson/Autotools, not cmake. The public headers are in dbus/.
# One generated header is needed: dbus/dbus-arch-deps.h (platform-specific typedefs).
FetchContent_Declare(libdbus_ext
    GIT_REPOSITORY https://gitlab.freedesktop.org/dbus/dbus.git
    GIT_TAG        dbus-1.14.10
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(libdbus_ext)
if(NOT libdbus_ext_POPULATED)
    FetchContent_Populate(libdbus_ext)

    # Generate dbus-arch-deps.h from the template
    # This header provides platform-specific type sizes for the D-Bus wire protocol
    file(WRITE "${libdbus_ext_SOURCE_DIR}/dbus/dbus-arch-deps.h"
"/* Generated for DTT -fsyntax-only tests */
#ifndef DBUS_ARCH_DEPS_H
#define DBUS_ARCH_DEPS_H

#include <stdint.h>

typedef int64_t  dbus_int64_t;
typedef uint64_t dbus_uint64_t;
typedef int32_t  dbus_int32_t;
typedef uint32_t dbus_uint32_t;
typedef int16_t  dbus_int16_t;
typedef uint16_t dbus_uint16_t;

#define DBUS_INT64_CONSTANT(val)  (INT64_C(val))
#define DBUS_UINT64_CONSTANT(val) (UINT64_C(val))

/* Alignment of the bus message body */
#define DBUS_MAXIMUM_MESSAGE_LENGTH 134217728
#define DBUS_MAXIMUM_MESSAGE_UNIX_FDS 1024

#endif /* DBUS_ARCH_DEPS_H */
")
endif()

dtt_add_external_lib_tests(
    LIB_PREFIX   libdbus
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/libdbus.dtt
    INCLUDE_DIRS "${libdbus_ext_SOURCE_DIR}"
)
