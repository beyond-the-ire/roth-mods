#ifndef HWR_PERF_H
#define HWR_PERF_H

#include <stdint.h>

/* Map-build timing instrument. HWR_PERF_LOG=1 wall-clocks the deferred map
 * reload + scene rebuild that a save load or map transition triggers, with a
 * per-phase breakdown, and reports the peak resident set around the build. The
 * whole facility compiles to near-nothing and does nothing at all unless the
 * env gate is set, so it is safe to leave the stamp calls on the build path. */

/* Cached HWR_PERF_LOG gate. */
int hwr_perf_enabled(void);

/* Monotonic nanosecond clock (CLOCK_MONOTONIC / QueryPerformanceCounter). */
uint64_t hwr_perf_now_ns(void);

/* Complete steady-frame timing under HWR_FRAME_PERF. The caller marks the
 * boundary after each phase in order; every 60 completed samples the profiler
 * reports both the whole override and the individual contributions. */
enum hwr_frame_perf_phase {
    HWR_FRAME_PERF_PREFLIGHT,
    HWR_FRAME_PERF_ENGINE_WALK,
    HWR_FRAME_PERF_PREPARE,
    HWR_FRAME_PERF_RENDER,
    HWR_FRAME_PERF_COMPOSITE,
    HWR_FRAME_PERF_SUBMIT,
    HWR_FRAME_PERF_FINALIZE,
    HWR_FRAME_PERF_PHASE_COUNT
};

struct hwr_frame_perf_sample {
    uint64_t start;
    uint64_t last;
    uint64_t phase[HWR_FRAME_PERF_PHASE_COUNT];
    int active;
};

void hwr_frame_perf_begin(struct hwr_frame_perf_sample *sample);
void hwr_frame_perf_mark(struct hwr_frame_perf_sample *sample,
                         enum hwr_frame_perf_phase phase);
void hwr_frame_perf_finish(struct hwr_frame_perf_sample *sample);

/* Starts a fresh timing session labelled `generation`; captures t0. A no-op
 * when the gate is off. Always resets, so a session left un-reported by an
 * aborted build cannot poison the next one. */
void hwr_perf_begin(uint32_t generation);

/* Records the time elapsed since the previous mark under `name` (a string
 * literal; only the pointer is retained) and advances the mark. The call order
 * defines the phase sequence. A no-op when no session is active. */
void hwr_perf_phase(const char *name);

/* Emits the accumulated phase table, the total, and the current / peak resident
 * set, then closes the session. A no-op when no session is active. */
void hwr_perf_report(void);

/* True while a session is open (a report is still owed). */
int hwr_perf_active(void);

#endif
