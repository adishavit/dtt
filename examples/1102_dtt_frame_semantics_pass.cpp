// 1102_dtt_frame_semantics_pass.cpp
// Tests that .dtt-annotated functions preserve frame semantics:
// ghost fields NOT mentioned in postconditions should survive the call.
//
// Bug: hasDTTAnnotations() only checks C++ [[clang::annotate("dtt:...")]]
// attributes, not FreeFunctionAnnotations from .dtt files. So .dtt-annotated
// functions are treated as "unannotated", triggering argument invalidation
// that wipes ghost fields set by prior postconditions.

struct Resource;

extern "C" {
    Resource* frame_init();           // ensures result._nonnull=1, result.@state=1
    void frame_query(Resource*);      // requires _nonnull!=0 (no postconditions)
    void frame_advance(Resource*);    // requires @state==1, ensures @state=2
    void frame_finish(Resource*);     // requires @state==2, ensures @state=0
}

// Variant 1: query (no postconditions) should not wipe @state
void test_query_preserves_state() {
    Resource* r = frame_init();  // r._nonnull=1, r.@state=1

    // frame_query has .dtt annotation but no postconditions on @state.
    // With proper frame semantics, @state should survive.
    frame_query(r);

    // frame_advance requires @state==1 — should still hold
    frame_advance(r);  // @state -> 2

    frame_finish(r);   // @state -> 0
}

// Variant 2: multiple queries between state transitions
void test_multiple_queries() {
    Resource* r = frame_init();  // @state=1

    frame_query(r);  // should preserve @state=1
    frame_query(r);  // should preserve @state=1
    frame_query(r);  // should preserve @state=1

    frame_advance(r);  // requires @state==1, should succeed

    frame_query(r);  // should preserve @state=2

    frame_finish(r);  // requires @state==2, should succeed
}
