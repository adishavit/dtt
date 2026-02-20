// FFmpeg miscellaneous utilities -- correct usage (should COMPILE)
//
// Tests memory allocation, logging, and timestamp utility functions.
// These use simple nonnull preconditions or have no preconditions at all.
//
// Functions tested: av_malloc, av_mallocz, av_realloc, av_free, av_freep,
//   av_strdup, av_log, av_log_set_level, av_log_get_level,
//   av_rescale_q, av_rescale_q_rnd, av_compare_ts

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/log.h>
#include <libavutil/rational.h>
#include <libavutil/mathematics.h>
}

// Variant 1: Memory allocation and free
void variant_memory() {
    void *buf = av_malloc(1024);                           // ensures result._nonnull = 1
    av_free(buf);

    void *zbuf = av_mallocz(256);                          // ensures result._nonnull = 1
    zbuf = av_realloc(zbuf, 512);
    av_free(zbuf);

    void *ptr = av_malloc(64);
    av_freep(&ptr);                                        // requires arg0 nonnull
}

// Variant 2: String duplication
void variant_strdup() {
    char *s = av_strdup("hello ffmpeg");                   // requires arg0 nonnull, ensures result nonnull
    av_free(s);

    char *s2 = av_strdup("test");
    av_free(s2);
}

// Variant 3: Logging
void variant_logging() {
    av_log_set_level(32);                                  // AV_LOG_INFO = 32
    av_log_get_level();

    av_log(nullptr, 32, "test message: %s\n");             // requires arg2 nonnull
    av_log(nullptr, 48, "debug: %d\n");                    // AV_LOG_DEBUG = 48
}

// Variant 4: Timestamp rescaling
void variant_timestamps() {
    AVRational tb1 = {1, 90000};
    AVRational tb2 = {1, 48000};
    AVRational tb3 = {1, 1000};

    av_rescale_q(90000, tb1, tb2);
    av_rescale_q(45000, tb1, tb3);
    av_rescale_q_rnd(90000, tb1, tb2, (enum AVRounding)0);

    av_compare_ts(1000, tb1, 500, tb2);
    av_compare_ts(0, tb1, 0, tb3);
}

int main() {
    variant_memory();
    variant_strdup();
    variant_logging();
    variant_timestamps();
    return 0;
}
