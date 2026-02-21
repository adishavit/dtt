// FFmpeg SwResample advanced operations -- direct arg0 patterns (should COMPILE)
//
// Tests additional SwResample functions beyond the basic lifecycle already
// covered in ffmpeg_swr_lifecycle_pass.cpp. All these functions use direct
// arg0 preconditions (not deref), so DTT can fully track them.
//
// Note: swr_alloc_set_opts was removed in FFmpeg 7.x (replaced by
// swr_alloc_set_opts2) so it cannot be tested with n7.1 headers.
//
// Functions tested: swr_is_initialized, swr_get_delay, swr_next_pts,
//   swr_convert_frame

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
}

// Variant 1: swr_is_initialized before and after init
void variant_is_initialized() {
    struct SwrContext *swr = swr_alloc();                  // @swr_alive = 1

    swr_is_initialized(swr);                              // requires arg0._nonnull != 0

    swr_init(swr);                                        // @swr_alive = 2

    swr_is_initialized(swr);                              // check after init

    swr_free(&swr);                                       // @swr_alive = 0
}

// Variant 2: swr_get_delay and swr_next_pts after init
void variant_delay_and_pts() {
    struct SwrContext *swr = swr_alloc();                  // @swr_alive = 1
    swr_init(swr);                                        // @swr_alive = 2

    swr_get_delay(swr, 48000);                            // requires @swr_alive == 2
    swr_next_pts(swr, 0);                                 // requires @swr_alive == 2

    swr_close(swr);                                       // @swr_alive = 1
    swr_free(&swr);                                       // @swr_alive = 0
}

// Variant 3: swr_convert_frame with AVFrame objects
void variant_convert_frame() {
    struct SwrContext *swr = swr_alloc();                  // @swr_alive = 1
    swr_init(swr);                                        // @swr_alive = 2

    AVFrame *out_frame = av_frame_alloc();                // @frame_alive = 1
    AVFrame *in_frame  = av_frame_alloc();                // @frame_alive = 1

    swr_convert_frame(swr, out_frame, in_frame);          // requires @swr_alive == 2

    av_frame_free(&in_frame);
    av_frame_free(&out_frame);
    swr_free(&swr);
}

int main() {
    variant_is_initialized();
    variant_delay_and_pts();
    variant_convert_frame();
    return 0;
}
