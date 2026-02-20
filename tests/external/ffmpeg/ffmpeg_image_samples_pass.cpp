// FFmpeg image and sample utility functions -- correct usage (should COMPILE)
//
// Tests av_image_* and av_samples_* utility functions. These use simple
// nonnull preconditions on buffer arguments with no ghost field state machines.
//
// Functions tested: av_image_alloc, av_image_fill_arrays,
//   av_image_get_buffer_size, av_samples_alloc, av_samples_get_buffer_size

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

// Variant 1: av_image_get_buffer_size query (no pointers needed)
void variant_image_buffer_size() {
    av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 1920, 1080, 1);
    av_image_get_buffer_size(AV_PIX_FMT_RGB24, 640, 480, 32);
}

// Variant 2: av_image_alloc with buffer pointers
void variant_image_alloc() {
    uint8_t *pointers[4] = {};
    int linesizes[4] = {};

    av_image_alloc(pointers, linesizes,                    // requires arg0/arg1 nonnull
                   1920, 1080, AV_PIX_FMT_YUV420P, 1);
}

// Variant 3: av_image_fill_arrays with pre-allocated buffer
void variant_image_fill_arrays() {
    uint8_t *dst_data[4] = {};
    int dst_linesize[4] = {};
    uint8_t src_buffer[1024] = {};

    av_image_fill_arrays(dst_data, dst_linesize,           // requires arg0/arg1/arg2 nonnull
                          src_buffer,
                          AV_PIX_FMT_RGB24, 16, 16, 1);
}

// Variant 4: av_samples_get_buffer_size query (no pointers needed)
void variant_samples_buffer_size() {
    av_samples_get_buffer_size(nullptr, 2, 1024, AV_SAMPLE_FMT_S16, 1);
    av_samples_get_buffer_size(nullptr, 1, 4096, AV_SAMPLE_FMT_FLT, 0);
}

// Variant 5: av_samples_alloc with buffer pointers
void variant_samples_alloc() {
    uint8_t *audio_data[1] = {};

    av_samples_alloc(audio_data, nullptr,                  // requires arg0 nonnull
                      2, 1024, AV_SAMPLE_FMT_S16, 0);
}

int main() {
    variant_image_buffer_size();
    variant_image_alloc();
    variant_image_fill_arrays();
    variant_samples_buffer_size();
    variant_samples_alloc();
    return 0;
}
