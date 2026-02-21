// FFmpeg SwScale lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost field @sws_alive correctly tracks the SwScale context
// through getContext (1) -> scale -> freeContext (0). Also tests sws_scale_frame
// and format query functions.
//
// Note: sws_getCachedContext returns a new context with @sws_alive = 1 but does
// not establish scope_exit, while the original context from sws_getContext has a
// scope_exit constraint. Testing getCachedContext requires careful handling to
// avoid scope-exit violations on the original context.
//
// Functions tested: sws_getContext, sws_scale, sws_freeContext,
//   sws_getCachedContext, sws_scale_frame, sws_isSupportedInput,
//   sws_isSupportedOutput

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// Variant 1: Full sws lifecycle with raw buffer scale
void variant_sws_lifecycle() {
    struct SwsContext *sws = sws_getContext(              // @sws_alive = 1
        1920, 1080, AV_PIX_FMT_YUV420P,
        1280, 720, AV_PIX_FMT_RGB24,
        0, nullptr, nullptr, nullptr);

    uint8_t src_plane[16] = {};
    const uint8_t *src_data[1] = {src_plane};
    int src_linesize[1] = {1920 * 3};
    uint8_t dst_plane[16] = {};
    uint8_t *dst_data[1] = {dst_plane};
    int dst_linesize[1] = {1280 * 3};

    sws_scale(sws,                                       // requires @sws_alive != 0
              src_data, src_linesize,
              0, 1080,
              dst_data, dst_linesize);

    sws_freeContext(sws);                                // @sws_alive = 0
}

// Variant 2: getCachedContext reuse pattern
void variant_sws_cached() {
    struct SwsContext *sws = sws_getCachedContext(         // @sws_alive = 1
        nullptr,
        640, 480, AV_PIX_FMT_YUV420P,
        320, 240, AV_PIX_FMT_RGB24,
        0, nullptr, nullptr, nullptr);

    // Reuse with same parameters
    struct SwsContext *sws2 = sws_getCachedContext(sws,   // @sws_alive = 1
        640, 480, AV_PIX_FMT_YUV420P,
        320, 240, AV_PIX_FMT_RGB24,
        0, nullptr, nullptr, nullptr);

    sws_freeContext(sws2);                                // @sws_alive = 0
}

// Variant 3: sws_scale_frame with AVFrame objects
void variant_sws_scale_frame() {
    struct SwsContext *sws = sws_getContext(              // @sws_alive = 1
        1920, 1080, AV_PIX_FMT_YUV420P,
        1280, 720, AV_PIX_FMT_RGB24,
        0, nullptr, nullptr, nullptr);

    AVFrame *dst = av_frame_alloc();                     // @frame_alive = 1
    AVFrame *src = av_frame_alloc();                     // @frame_alive = 1

    sws_scale_frame(sws, dst, src);                      // requires @sws_alive != 0

    av_frame_free(&src);
    av_frame_free(&dst);
    sws_freeContext(sws);
}

// Variant 4: Format support queries (no state requirements)
void variant_sws_format_queries() {
    sws_isSupportedInput(AV_PIX_FMT_YUV420P);
    sws_isSupportedOutput(AV_PIX_FMT_RGB24);
    sws_isSupportedInput(AV_PIX_FMT_NV12);
    sws_isSupportedOutput(AV_PIX_FMT_BGR24);
}

int main() {
    variant_sws_lifecycle();
    variant_sws_cached();
    variant_sws_scale_frame();
    variant_sws_format_queries();
    return 0;
}
