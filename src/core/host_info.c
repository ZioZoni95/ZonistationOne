/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 *
 * host_info.c — see host_info.h.
 *
 * Everything here is read from /proc, /sys and uname. Nothing is inferred and
 * nothing is typed in: a field that cannot be read stays "unknown" rather than
 * being filled with something plausible.
 */
#define _GNU_SOURCE
#include "host_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/utsname.h>

static HostInfo g_host;
static bool     g_static_done = false;

/* Per-thread jiffy totals from the previous sample, so a delta can be taken. */
typedef struct { int tid; unsigned long ticks; } ThreadTick;
static ThreadTick g_prev_ticks[HOST_MAX_THREADS];
static int        g_prev_tick_count = 0;
static double     g_prev_sample_t   = 0.0;
static unsigned long g_prev_proc_ticks = 0;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void copy_trim(char* dst, size_t n, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    while (*src == ' ' || *src == '\t' || *src == '"') src++;
    size_t len = strlen(src);
    while (len && (src[len - 1] == '\n' || src[len - 1] == ' ' ||
                   src[len - 1] == '\t' || src[len - 1] == '"')) len--;
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Read a whole one-line sysfs/procfs file. Returns false when it is not there. */
static bool read_line_file(const char* path, char* out, size_t n) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char buf[256];
    bool ok = fgets(buf, sizeof(buf), f) != NULL;
    fclose(f);
    if (ok) copy_trim(out, n, buf);
    return ok;
}

static void read_cpu_model(void) {
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (!f) { copy_trim(g_host.cpu_model, sizeof(g_host.cpu_model), "unknown CPU"); return; }
    char line[512];
    int  core_ids[256], core_id_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!g_host.cpu_model[0] && strncmp(line, "model name", 10) == 0) {
            const char* colon = strchr(line, ':');
            if (colon) copy_trim(g_host.cpu_model, sizeof(g_host.cpu_model), colon + 1);
        } else if (strncmp(line, "core id", 7) == 0) {
            const char* colon = strchr(line, ':');
            if (colon) {
                int id = atoi(colon + 1), seen = 0;
                for (int i = 0; i < core_id_count; i++) if (core_ids[i] == id) { seen = 1; break; }
                if (!seen && core_id_count < 256) core_ids[core_id_count++] = id;
            }
        }
    }
    fclose(f);
    if (!g_host.cpu_model[0]) copy_trim(g_host.cpu_model, sizeof(g_host.cpu_model), "unknown CPU");
    g_host.cpu_cores = core_id_count > 0 ? core_id_count : 0;
}

static void read_static(void) {
    char vendor[64] = {0}, product[64] = {0};
    read_line_file("/sys/devices/virtual/dmi/id/sys_vendor", vendor, sizeof(vendor));
    read_line_file("/sys/devices/virtual/dmi/id/product_name", product, sizeof(product));
    if (vendor[0] || product[0])
        snprintf(g_host.system, sizeof(g_host.system), "%s%s%s", vendor, (vendor[0] && product[0]) ? " " : "", product);
    else
        snprintf(g_host.system, sizeof(g_host.system), "unknown");

    read_cpu_model();
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    g_host.cpu_threads = online > 0 ? (int)online : 0;

    struct utsname un;
    if (uname(&un) == 0)
        /* Bounded explicitly: utsname's fields are 65 bytes each and the panel
         * has room for one line, not two. */
        snprintf(g_host.kernel, sizeof(g_host.kernel), "%.48s %.16s", un.release, un.machine);
    else
        snprintf(g_host.kernel, sizeof(g_host.kernel), "unknown");

    FILE* f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                copy_trim(g_host.distro, sizeof(g_host.distro), line + 12);
                break;
            }
        }
        fclose(f);
    }
    if (!g_host.distro[0]) copy_trim(g_host.distro, sizeof(g_host.distro), "unknown");

    copy_trim(g_host.gl_vendor,   sizeof(g_host.gl_vendor),   "not yet queried");
    copy_trim(g_host.gl_renderer, sizeof(g_host.gl_renderer), "not yet queried");
    copy_trim(g_host.gl_version,  sizeof(g_host.gl_version),  "-");
    copy_trim(g_host.gl_driver,   sizeof(g_host.gl_driver),   "-");
    copy_trim(g_host.audio_driver, sizeof(g_host.audio_driver), "none");

    g_static_done = true;
}

const HostInfo* host_info_get(void) {
    if (!g_static_done) read_static();
    return &g_host;
}

void host_info_set_gl(const char* vendor, const char* renderer, const char* version,
                      const char* driver, const char* request, bool honoured) {
    if (!g_static_done) read_static();
    copy_trim(g_host.gl_vendor,   sizeof(g_host.gl_vendor),   vendor   ? vendor   : "unknown");
    copy_trim(g_host.gl_renderer, sizeof(g_host.gl_renderer), renderer ? renderer : "unknown");
    copy_trim(g_host.gl_version,  sizeof(g_host.gl_version),  version  ? version  : "unknown");
    copy_trim(g_host.gl_driver,   sizeof(g_host.gl_driver),   driver   ? driver   : "unknown");
    copy_trim(g_host.gpu_request, sizeof(g_host.gpu_request), request  ? request  : "");
    g_host.gpu_request_honoured = honoured;
}

void host_info_set_audio(const char* driver, int freq, int channels, int buffer_frames) {
    if (!g_static_done) read_static();
    copy_trim(g_host.audio_driver, sizeof(g_host.audio_driver), driver ? driver : "unknown");
    g_host.audio_freq = freq;
    g_host.audio_channels = channels;
    g_host.audio_buffer_frames = buffer_frames;
}

double host_info_audio_latency_ms(void) {
    if (g_host.audio_freq <= 0 || g_host.audio_buffer_frames <= 0) return 0.0;
    return 1000.0 * (double)g_host.audio_buffer_frames / (double)g_host.audio_freq;
}

/* /proc/self/task/<tid>/stat: field 2 is the comm in parentheses (which can
 * itself contain spaces and parentheses), fields 14 and 15 are utime/stime in
 * clock ticks. Parsing from the last ')' is what makes a comm like "GPU (2)"
 * harmless. */
static bool read_thread_stat(int tid, char* comm, size_t comm_n, unsigned long* ticks) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (!n) return false;
    buf[n] = '\0';

    char* close = strrchr(buf, ')');
    char* open  = strchr(buf, '(');
    if (!open || !close || close < open) return false;
    size_t len = (size_t)(close - open - 1);
    if (len >= comm_n) len = comm_n - 1;
    memcpy(comm, open + 1, len);
    comm[len] = '\0';

    /* After the comm come the remaining fields, space separated: field 3 is the
     * state character and utime/stime are fields 14 and 15 of the whole line,
     * i.e. the 12th and 13th token after the comm. Walked by hand rather than
     * with a scanf of a dozen suppressed conversions, which is both unreadable
     * and something gcc warns about. */
    const char* tok = close + 2;
    for (int field = 3; field < 14 && tok && *tok; field++) {
        tok = strchr(tok, ' ');
        if (tok) tok++;
    }
    if (!tok || !*tok) return false;
    char* endp = NULL;
    unsigned long utime = strtoul(tok, &endp, 10);
    if (endp == tok) return false;
    unsigned long stime = strtoul(endp, &endp, 10);
    *ticks = utime + stime;
    return true;
}

void host_info_sample(void) {
    if (!g_static_done) read_static();

    double t = now_seconds();
    double dt = t - g_prev_sample_t;
    if (g_prev_sample_t > 0.0 && dt < 0.5) return;

    /* Memory: MemTotal / MemAvailable, and this process's RSS. */
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        unsigned long total_kb = 0, avail_kb = 0;
        while (fgets(line, sizeof(line), f)) {
            if      (sscanf(line, "MemTotal: %lu kB", &total_kb) == 1) continue;
            else if (sscanf(line, "MemAvailable: %lu kB", &avail_kb) == 1) break;
        }
        fclose(f);
        g_host.ram_total_mb = (double)total_kb / 1024.0;
        g_host.ram_avail_mb = (double)avail_kb / 1024.0;
    }
    f = fopen("/proc/self/statm", "r");
    if (f) {
        unsigned long size_pages = 0, rss_pages = 0;
        if (fscanf(f, "%lu %lu", &size_pages, &rss_pages) == 2)
            g_host.rss_mb = (double)rss_pages * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
        fclose(f);
    }

    /* Per-thread CPU, as a share of one core over the sample window. */
    const double hz = (double)sysconf(_SC_CLK_TCK);
    ThreadTick cur[HOST_MAX_THREADS];
    int cur_count = 0;
    unsigned long proc_ticks = 0;
    int self_pid = (int)getpid();

    DIR* d = opendir("/proc/self/task");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL && cur_count < HOST_MAX_THREADS) {
            if (!isdigit((unsigned char)e->d_name[0])) continue;
            int tid = atoi(e->d_name);
            char comm[24] = {0};
            unsigned long ticks = 0;
            if (!read_thread_stat(tid, comm, sizeof(comm), &ticks)) continue;

            proc_ticks += ticks;
            cur[cur_count].tid = tid;
            cur[cur_count].ticks = ticks;

            HostThreadLoad* th = &g_host.threads[cur_count];
            th->tid = tid;
            th->is_main = (tid == self_pid);
            snprintf(th->name, sizeof(th->name), "%s", comm);

            th->cpu_pct = 0.0;
            if (dt > 0.0) {
                for (int i = 0; i < g_prev_tick_count; i++) {
                    if (g_prev_ticks[i].tid != tid) continue;
                    unsigned long delta = ticks - g_prev_ticks[i].ticks;
                    th->cpu_pct = 100.0 * (double)delta / (hz * dt);
                    break;
                }
            }
            cur_count++;
        }
        closedir(d);
    }
    g_host.thread_count = cur_count;

    if (dt > 0.0 && g_prev_proc_ticks) {
        unsigned long delta = proc_ticks - g_prev_proc_ticks;
        g_host.process_cpu_pct = 100.0 * (double)delta / (hz * dt);
    }
    g_prev_proc_ticks = proc_ticks;

    memcpy(g_prev_ticks, cur, sizeof(ThreadTick) * (size_t)cur_count);
    g_prev_tick_count = cur_count;
    g_prev_sample_t = t;
}
