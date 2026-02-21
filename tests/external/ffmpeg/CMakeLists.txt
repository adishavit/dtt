# FFmpeg: FetchContent + generate minimal config headers
# FFmpeg uses a custom configure script (not cmake). Headers are in
# libavformat/, libavcodec/, libavutil/, etc. at the repo root.
# Two small generated headers are needed for -fsyntax-only compilation:
#   - libavutil/avconfig.h (pixel format endianness)
#   - libavutil/ffversion.h (version string)
FetchContent_Declare(ffmpeg_ext
    GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
    GIT_TAG        n7.1
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(ffmpeg_ext)
if(NOT ffmpeg_ext_POPULATED)
    FetchContent_Populate(ffmpeg_ext)

    # Generate avconfig.h (defines AV_HAVE_BIGENDIAN etc.)
    file(WRITE "${ffmpeg_ext_SOURCE_DIR}/libavutil/avconfig.h"
"/* Generated for DTT -fsyntax-only tests */
#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN 0
#define AV_HAVE_FAST_UNALIGNED 1
#endif
")

    # Generate ffversion.h
    file(WRITE "${ffmpeg_ext_SOURCE_DIR}/libavutil/ffversion.h"
"/* Generated for DTT -fsyntax-only tests */
#ifndef AVUTIL_FFVERSION_H
#define AVUTIL_FFVERSION_H
#define FFMPEG_VERSION \"n7.1-dtt\"
#endif
")
endif()

dtt_add_external_lib_tests(
    LIB_PREFIX   ffmpeg
    DTT_FILE     ${CMAKE_SOURCE_DIR}/annotations/external/ffmpeg.dtt
    INCLUDE_DIRS "${ffmpeg_ext_SOURCE_DIR}"
)
