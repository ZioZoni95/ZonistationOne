/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef HOST_INFO_H
#define HOST_INFO_H

#include <stdbool.h>
#include <stdint.h>

/* What the host machine actually is, read from the kernel rather than typed in.
 *
 * The Host HW panel used to carry hard-coded strings — a laptop model, a CPU, a
 * driver version, four thread-load bars with constants in them. A panel that
 * says "RTX 4060" on a machine that is running on the iGPU is worse than no
 * panel: the whole point of that line is to answer "which GPU got the context"
 * before a rendering difference is blamed on the emulator.
 *
 * Static fields are read once. Live fields (memory, per-thread CPU) are
 * refreshed by host_info_sample(), which is rate-limited internally and does
 * nothing if called more often — the panels cost, not the core. */

#define HOST_MAX_THREADS 12

typedef struct {
    char     name[24];        /* thread comm, e.g. "GPU", "cdrom-read" */
    int      tid;
    double   cpu_pct;         /* share of one core, 0..100 per core */
    bool     is_main;
} HostThreadLoad;

typedef struct {
    /* Static — filled on first host_info_get() */
    char system[160];         /* DMI vendor + product, or "unknown" */
    char cpu_model[96];
    int  cpu_cores;           /* physical, best effort */
    int  cpu_threads;         /* online logical CPUs */
    char kernel[80];          /* uname release + machine */
    char distro[64];          /* PRETTY_NAME from os-release */

    /* Set by the host shell once the GL context and audio device exist */
    char gl_vendor[96];
    char gl_renderer[128];
    char gl_version[64];
    char gl_driver[48];       /* "NVIDIA proprietary" / "Mesa (Intel)" / ... */
    char gpu_request[16];     /* ZS1_GPU as asked for, "" if unset */
    bool gpu_request_honoured;

    char audio_driver[32];    /* SDL audio driver, e.g. "pipewire" */
    int  audio_freq;
    int  audio_channels;
    int  audio_buffer_frames;

    /* Live — refreshed by host_info_sample() */
    double ram_total_mb;
    double ram_avail_mb;
    double rss_mb;
    double process_cpu_pct;   /* whole process, share of one core */
    int    thread_count;
    HostThreadLoad threads[HOST_MAX_THREADS];
} HostInfo;

/* The singleton. Static fields are read on the first call. */
const HostInfo* host_info_get(void);

void host_info_set_gl(const char* vendor, const char* renderer, const char* version,
                      const char* driver, const char* request, bool honoured);
void host_info_set_audio(const char* driver, int freq, int channels, int buffer_frames);

/* Refresh the live fields. Rate-limited to ~0.5 s; safe to call every frame. */
void host_info_sample(void);

/* Audio latency implied by the device buffer, in ms (0 if unknown). */
double host_info_audio_latency_ms(void);

#endif /* HOST_INFO_H */
