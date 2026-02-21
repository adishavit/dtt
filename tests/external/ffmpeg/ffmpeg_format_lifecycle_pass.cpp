// FFmpeg format context lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost field @format_state correctly tracks the format
// context lifecycle through alloc (1) -> free (0).
//
// Note: avformat_open_input and avformat_alloc_output_context2 use deref
// postconditions ("ensures arg0.deref.@format_state = N") which DTT cannot
// currently resolve for free functions. Those patterns are tested in
// ffmpeg_format_deref_xfail.cpp. This test covers non-deref patterns only.

extern "C" {
#include <libavformat/avformat.h>
}

// Variant 1: alloc_context -> free_context (basic lifecycle, no deref)
void variant_alloc_free() {
    AVFormatContext *fmt_ctx = avformat_alloc_context();  // @format_state = 1
    avformat_free_context(fmt_ctx);                       // @format_state = 0
}

int main() {
    variant_alloc_free();
    return 0;
}
