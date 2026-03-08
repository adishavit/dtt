#!/bin/bash
# Magma Benchmark Harness for DTT
# Runs DTT plugin on all Magma bug-containing source files
# and reports which bugs produce DTT diagnostics.

set -uo pipefail

DTT_PLUGIN="/home/user/dtt/build/libDTTPlugin.so"
MAGMA_DIR="/home/user/magma"
WORK_DIR="/home/user/magma_workdir"
RESULTS_DIR="/home/user/dtt/benchmark/results"
CC="clang-18"

mkdir -p "$RESULTS_DIR"

# Summary counters
TOTAL_FILES=0
TOTAL_BUGS=0
FILES_WITH_DTT_DIAG=0
BUGS_FLAGGED=0

run_dtt_on_file() {
    local target="$1"
    local file="$2"
    local bug_id="$3"
    local includes="$4"
    local extra_flags="$5"

    local full_path="$WORK_DIR/$target/repo/$file"
    if [ ! -f "$full_path" ]; then
        echo "  SKIP $bug_id: $file not found"
        return 1
    fi

    TOTAL_FILES=$((TOTAL_FILES + 1))

    local outfile="$RESULTS_DIR/${target}_${bug_id}.txt"

    # Run DTT in syntax-only mode, capture all output (stdout + stderr)
    set +e
    output=$($CC -fplugin="$DTT_PLUGIN" \
        -std=c11 -fsyntax-only \
        $includes $extra_flags \
        "$full_path" 2>&1)
    local exit_code=$?
    set -e

    echo "$output" > "$outfile"

    # Check for DTT-specific diagnostics (categories C/D/F/G/H, or [dtt] tags)
    local dtt_lines=""
    dtt_lines=$(echo "$output" | grep -iE "\[dtt\]|DTT:|category [CDFGH]|_size|_nonnull|_has_value|@open|@allocated|_tainted|use-after|double-free|uninitialized|resource leak|null pointer|buffer overflow|out.of.bounds" || true)

    # Also check for general clang warnings/errors that DTT might produce
    local warn_lines=""
    warn_lines=$(echo "$output" | grep -cE "warning:|error:" || true)

    if [ -n "$dtt_lines" ]; then
        echo "  HIT  $bug_id ($file): DTT flagged issues"
        echo "$dtt_lines" | head -5 | sed 's/^/       /'
        FILES_WITH_DTT_DIAG=$((FILES_WITH_DTT_DIAG + 1))
        return 0
    elif [ "$warn_lines" -gt 0 ] 2>/dev/null; then
        echo "  WARN $bug_id ($file): $warn_lines warnings/errors (non-DTT)"
        return 2
    else
        echo "  MISS $bug_id ($file): no diagnostics"
        return 3
    fi
}

echo "=============================================="
echo "  DTT vs Magma Benchmark"
echo "=============================================="
echo ""

# ===== libpng =====
echo "--- libpng (7 bugs) ---"
LIBPNG_INC="-I$WORK_DIR/libpng/repo -I$WORK_DIR/libpng/repo/scripts -DHAVE_CONFIG_H"
# libpng needs pnglibconf.h and config.h
if [ ! -f "$WORK_DIR/libpng/repo/pnglibconf.h" ]; then
    cp "$WORK_DIR/libpng/repo/scripts/pnglibconf.h.prebuilt" \
       "$WORK_DIR/libpng/repo/pnglibconf.h" 2>/dev/null || true
fi
if [ ! -f "$WORK_DIR/libpng/repo/config.h" ]; then
    echo '#define HAVE_LIBZ 1' > "$WORK_DIR/libpng/repo/config.h"
fi

for bug_patch in "$MAGMA_DIR"/targets/libpng/patches/bugs/*.patch; do
    bug_id=$(basename "$bug_patch" .patch)
    TOTAL_BUGS=$((TOTAL_BUGS + 1))
    # Get first .c file from the patch
    file=$(grep "^diff --git a/" "$bug_patch" | head -1 | sed 's|diff --git a/||;s| b/.*||')
    if [[ "$file" == *.c ]]; then
        run_dtt_on_file "libpng" "$file" "$bug_id" "$LIBPNG_INC" "-DPNG_ARM_NEON_OPT=0" || true
    fi
done
echo ""

# ===== lua =====
echo "--- lua (4 bugs) ---"
LUA_INC="-I$WORK_DIR/lua/repo -DLUA_USE_LINUX"

for bug_patch in "$MAGMA_DIR"/targets/lua/patches/bugs/*.patch; do
    bug_id=$(basename "$bug_patch" .patch)
    TOTAL_BUGS=$((TOTAL_BUGS + 1))
    file=$(grep "^diff --git a/" "$bug_patch" | head -1 | sed 's|diff --git a/||;s| b/.*||')
    if [[ "$file" == *.c ]]; then
        run_dtt_on_file "lua" "$file" "$bug_id" "$LUA_INC" "" || true
    fi
done
echo ""

# ===== libsndfile =====
echo "--- libsndfile (18 bugs) ---"
LIBSNDFILE_INC="-I$WORK_DIR/libsndfile/repo/src -I$WORK_DIR/libsndfile/repo/include -I$WORK_DIR/libsndfile/repo -DHAVE_EXTERNAL_XIPH_LIBS=1"
# Generate config.h and sndfile.h if needed
if [ ! -f "$WORK_DIR/libsndfile/repo/src/config.h" ]; then
    touch "$WORK_DIR/libsndfile/repo/src/config.h"
fi
if [ ! -f "$WORK_DIR/libsndfile/repo/src/sndfile.h" ]; then
    sed -e 's/@SNDFILE_VERSION_MAJOR@/1/g' \
        -e 's/@SNDFILE_VERSION_MINOR@/0/g' \
        -e 's/@SNDFILE_VERSION_PATCH@/0/g' \
        -e 's/@TYPEOF_SF_COUNT_T@/int64_t/g' \
        -e 's/@SF_COUNT_MAX@/0x7FFFFFFFFFFFFFFFLL/g' \
        -e 's/@SIZEOF_SF_COUNT_T@/8/g' \
        "$WORK_DIR/libsndfile/repo/include/sndfile.h.in" > "$WORK_DIR/libsndfile/repo/src/sndfile.h" 2>/dev/null || true
fi

for bug_patch in "$MAGMA_DIR"/targets/libsndfile/patches/bugs/*.patch; do
    bug_id=$(basename "$bug_patch" .patch)
    TOTAL_BUGS=$((TOTAL_BUGS + 1))
    file=$(grep "^diff --git a/" "$bug_patch" | head -1 | sed 's|diff --git a/||;s| b/.*||')
    if [[ "$file" == *.c ]]; then
        run_dtt_on_file "libsndfile" "$file" "$bug_id" "$LIBSNDFILE_INC" "" || true
    fi
done
echo ""

# ===== libtiff =====
echo "--- libtiff (14 bugs) ---"
LIBTIFF_INC="-I$WORK_DIR/libtiff/repo/libtiff -I$WORK_DIR/libtiff/repo"
# Generate tif_config.h and tiffconf.h if needed
if [ ! -f "$WORK_DIR/libtiff/repo/libtiff/tif_config.h" ]; then
    cat > "$WORK_DIR/libtiff/repo/libtiff/tif_config.h" <<'CONFEOF'
#ifndef TIF_CONFIG_H
#define TIF_CONFIG_H
#define HAVE_ASSERT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define SIZEOF_SIGNED_INT 4
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_SIGNED_LONG 8
#define SIZEOF_UNSIGNED_LONG 8
#define SIZEOF_SIGNED_SHORT 2
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_SIZE_TYPE 8
#define TIFF_INT8_T signed char
#define TIFF_UINT8_T unsigned char
#define TIFF_INT16_T signed short
#define TIFF_UINT16_T unsigned short
#define TIFF_INT32_T signed int
#define TIFF_UINT32_T unsigned int
#define TIFF_INT64_T signed long
#define TIFF_UINT64_T unsigned long
#define TIFF_SSIZE_T long
#define HOST_FILLORDER FILLORDER_LSB2MSB
#define HOST_BIGENDIAN 0
#endif
CONFEOF
fi
if [ ! -f "$WORK_DIR/libtiff/repo/libtiff/tiffconf.h" ]; then
    cat > "$WORK_DIR/libtiff/repo/libtiff/tiffconf.h" <<'CONFEOF2'
#ifndef TIFFCONF_H
#define TIFFCONF_H
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#define TIFF_INT8_T int8_t
#define TIFF_UINT8_T uint8_t
#define TIFF_INT16_T int16_t
#define TIFF_UINT16_T uint16_t
#define TIFF_INT32_T int32_t
#define TIFF_UINT32_T uint32_t
#define TIFF_INT64_T int64_t
#define TIFF_UINT64_T uint64_t
#define TIFF_SSIZE_T ssize_t
#define TIFF_SIZE_T size_t
#define TIFF_PTRDIFF_T ptrdiff_t
#define CCITT_SUPPORT 1
#define JPEG_SUPPORT 0
#define LOGLUV_SUPPORT 1
#define LZW_SUPPORT 1
#define NEXT_SUPPORT 1
#define PACKBITS_SUPPORT 1
#define PIXARLOG_SUPPORT 0
#define THUNDER_SUPPORT 1
#define ZIP_SUPPORT 0
#define STRIPCHOP_DEFAULT TIFF_STRIPCHOP
#define SUBIFD_SUPPORT 1
#define DEFAULT_EXTRASAMPLE_AS_ALPHA 1
#define CHECK_JPEG_YCBCR_SUBSAMPLING 1
#define MDI_SUPPORT 1
#endif
CONFEOF2
fi

for bug_patch in "$MAGMA_DIR"/targets/libtiff/patches/bugs/*.patch; do
    bug_id=$(basename "$bug_patch" .patch)
    TOTAL_BUGS=$((TOTAL_BUGS + 1))
    file=$(grep "^diff --git a/" "$bug_patch" | head -1 | sed 's|diff --git a/||;s| b/.*||')
    if [[ "$file" == *.c ]]; then
        run_dtt_on_file "libtiff" "$file" "$bug_id" "$LIBTIFF_INC" "" || true
    elif [[ "$file" == *.h ]]; then
        echo "  SKIP $bug_id: header-only patch ($file)"
    else
        echo "  SKIP $bug_id: no C file in patch"
    fi
done
echo ""

# ===== openssl =====
echo "--- openssl (20 bugs) ---"
OPENSSL_INC="-I$WORK_DIR/openssl/repo/include -I$WORK_DIR/openssl/repo -I$WORK_DIR/openssl/repo/crypto -I$WORK_DIR/openssl/repo/crypto/asn1 -I$WORK_DIR/openssl/repo/ssl"

for bug_patch in "$MAGMA_DIR"/targets/openssl/patches/bugs/*.patch; do
    bug_id=$(basename "$bug_patch" .patch)
    TOTAL_BUGS=$((TOTAL_BUGS + 1))
    files=$(grep "^diff --git a/" "$bug_patch" | sed 's|diff --git a/||;s| b/.*||')
    if [ -z "$files" ]; then
        echo "  SKIP $bug_id: empty patch"
        continue
    fi
    # Get first .c file
    file=$(echo "$files" | grep '\.c$' | head -1)
    if [ -z "$file" ]; then
        file=$(echo "$files" | head -1)
    fi
    if [[ "$file" == *.c ]]; then
        run_dtt_on_file "openssl" "$file" "$bug_id" "$OPENSSL_INC" "" || true
    elif [[ "$file" == *.h ]]; then
        echo "  SKIP $bug_id: header-only patch ($file)"
    else
        echo "  SKIP $bug_id: no C file"
    fi
done
echo ""

# ===== Summary =====
echo "=============================================="
echo "  SUMMARY"
echo "=============================================="
echo "Total bugs examined: $TOTAL_BUGS"
echo "Total files analyzed: $TOTAL_FILES"
echo "Files with DTT diagnostics: $FILES_WITH_DTT_DIAG"
echo ""
echo "Detailed results saved to: $RESULTS_DIR/"
echo ""
echo "NOTE: DTT is a single-TU syntax-only plugin. Many Magma bugs"
echo "require runtime context (heap state, I/O, crypto) that is"
echo "beyond static analysis scope. DTT can only flag bugs visible"
echo "as precondition violations, null derefs, buffer overflows,"
echo "use-after-free, taint propagation, etc. in isolated TUs."
