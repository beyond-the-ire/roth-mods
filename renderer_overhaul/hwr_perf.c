#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "hwr_perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define HWR_PERF_MAX_PHASES 24

struct hwr_perf_mark {
    const char *name;
    uint64_t ns;
};

static int g_perf_active;
static uint32_t g_perf_generation;
static uint64_t g_perf_t0;
static uint64_t g_perf_last;
static struct hwr_perf_mark g_perf_marks[HWR_PERF_MAX_PHASES];
static size_t g_perf_count;

int hwr_perf_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PERF_LOG");
        state = setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    return state;
}

uint64_t hwr_perf_now_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER freq;
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&freq) ||
        freq.QuadPart == 0)
        return 0;
    return (uint64_t)((double)counter.QuadPart * 1.0e9 / (double)freq.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static int hwr_frame_perf_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_FRAME_PERF");
        state = setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    return state;
}

void hwr_frame_perf_begin(struct hwr_frame_perf_sample *sample)
{
    uint64_t now;
    memset(sample, 0, sizeof *sample);
    if (!hwr_frame_perf_enabled())
        return;
    now = hwr_perf_now_ns();
    if (now == 0)
        return;
    sample->start = now;
    sample->last = now;
    sample->active = 1;
}

void hwr_frame_perf_mark(struct hwr_frame_perf_sample *sample,
                         enum hwr_frame_perf_phase phase)
{
    uint64_t now;
    if (!sample->active || phase < 0 || phase >= HWR_FRAME_PERF_PHASE_COUNT)
        return;
    now = hwr_perf_now_ns();
    if (now < sample->last) {
        sample->active = 0;
        return;
    }
    sample->phase[phase] += now - sample->last;
    sample->last = now;
}

void hwr_frame_perf_finish(struct hwr_frame_perf_sample *sample)
{
    static uint64_t phase_sum[HWR_FRAME_PERF_PHASE_COUNT];
    static uint64_t total_sum;
    static unsigned samples;
    static const char *const phase_name[HWR_FRAME_PERF_PHASE_COUNT] = {
        "preflight", "engine-walk", "prepare", "sun", "render",
        "composite", "submit", "finalize"
    };
    uint64_t total;
    size_t i;

    if (!sample->active)
        return;
    total = sample->last - sample->start;
    total_sum += total;
    for (i = 0; i < HWR_FRAME_PERF_PHASE_COUNT; i++)
        phase_sum[i] += sample->phase[i];
    samples++;
    sample->active = 0;

    if (samples >= 60u) {
        double divisor = (double)samples * 1.0e6;
        fprintf(stderr, "[renderer_overhaul] frame pipeline avg/%u: total=%.2fms",
                samples, total_sum / divisor);
        for (i = 0; i < HWR_FRAME_PERF_PHASE_COUNT; i++)
            fprintf(stderr, " %s=%.2fms", phase_name[i],
                    phase_sum[i] / divisor);
        fputc('\n', stderr);
        memset(phase_sum, 0, sizeof phase_sum);
        total_sum = 0;
        samples = 0;
    }
}

void hwr_perf_begin(uint32_t generation)
{
    if (!hwr_perf_enabled())
        return;
    g_perf_active = 1;
    g_perf_generation = generation;
    g_perf_count = 0;
    g_perf_t0 = hwr_perf_now_ns();
    g_perf_last = g_perf_t0;
}

void hwr_perf_phase(const char *name)
{
    uint64_t now;
    if (!g_perf_active)
        return;
    now = hwr_perf_now_ns();
    if (g_perf_count < HWR_PERF_MAX_PHASES) {
        g_perf_marks[g_perf_count].name = name;
        g_perf_marks[g_perf_count].ns = now - g_perf_last;
        g_perf_count++;
    }
    g_perf_last = now;
}

/* Reads the process resident set (current and peak) in kB. Returns 0 on
 * platforms/paths where it is unavailable; the report then prints n/a. */
static void hwr_perf_read_rss(unsigned long *rss_kb, unsigned long *peak_kb)
{
    *rss_kb = 0;
    *peak_kb = 0;
#ifndef _WIN32
    {
        FILE *file = fopen("/proc/self/status", "r");
        char line[256];
        if (file == NULL)
            return;
        while (fgets(line, sizeof line, file) != NULL) {
            unsigned long value = 0;
            if (sscanf(line, "VmRSS: %lu kB", &value) == 1)
                *rss_kb = value;
            else if (sscanf(line, "VmHWM: %lu kB", &value) == 1)
                *peak_kb = value;
        }
        fclose(file);
    }
#endif
}

void hwr_perf_report(void)
{
    uint64_t total;
    unsigned long rss_kb, peak_kb;
    size_t i;
    if (!g_perf_active)
        return;
    total = hwr_perf_now_ns() - g_perf_t0;
    hwr_perf_read_rss(&rss_kb, &peak_kb);
    fprintf(stderr, "[renderer_overhaul] map build gen=%u total=%.2fms",
            g_perf_generation, (double)total / 1.0e6);
    for (i = 0; i < g_perf_count; i++)
        fprintf(stderr, " %s=%.2f", g_perf_marks[i].name,
                (double)g_perf_marks[i].ns / 1.0e6);
    fprintf(stderr, " | RSS=%luKB peakRSS=%luKB\n", rss_kb, peak_kb);
    g_perf_active = 0;
}

int hwr_perf_active(void)
{
    return g_perf_active;
}
