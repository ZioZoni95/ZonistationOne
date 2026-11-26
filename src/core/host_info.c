/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 *
 * host_info.c — see host_info.h.
 *
 * Everything here is read from /proc, /sys and uname on Linux, and from the
 * registry and the Win32 process and thread APIs on Windows. Nothing is
 * inferred and nothing is typed in: a field that cannot be read stays "unknown"
 * rather than being filled with something plausible.
 */
#define _GNU_SOURCE
#include "host_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static HostInfo g_host;
static bool     g_static_done = false;

static void copy_trim(char* dst, size_t n, const char* src);
static void read_static(void);

#ifdef _WIN32
/* ------------------------------------------------------------------------- */
/* Windows                                                                    */
/* ------------------------------------------------------------------------- */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

/* Per-thread CPU time (100 ns units) from the previous sample. 64-bit: a
 * 32-bit count of 100 ns ticks wraps after seven minutes. */
typedef struct { DWORD tid; unsigned long long ticks; } ThreadTick;
static ThreadTick g_prev_ticks[HOST_MAX_THREADS];
static int        g_prev_tick_count = 0;
static double     g_prev_sample_t   = 0.0;
static unsigned long long g_prev_proc_ticks = 0;
/* The thread that first asks for host information. That is main(), through
 * init_sdl() -> host_info_set_gl(), before any other thread exists — the same
 * thread Linux marks as main by tid == pid. */
static DWORD      g_main_tid = 0;

static double now_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* One REG_SZ value under HKEY_LOCAL_MACHINE. */
static bool read_reg_string(const char* key, const char* value, char* out, size_t n) {
    char buf[256];
    DWORD size = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, NULL, buf, &size) != ERROR_SUCCESS)
        return false;
    copy_trim(out, n, buf);
    return out[0] != '\0';
}

static int count_physical_cores(void) {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len == 0 || len > 256 * 1024) return 0;
    static unsigned char buf[256 * 1024];   /* once, at startup; not a hot path */
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(void*)buf, &len))
        return 0;
    int cores = 0;
    for (DWORD off = 0; off < len; ) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e =
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(void*)(buf + off);
        if (e->Relationship == RelationProcessorCore) cores++;
        if (e->Size == 0) break;
        off += e->Size;
    }
    return cores;
}

/* GetVersionEx reports what the manifest allows, not what is running;
 * RtlGetVersion reports the real version. It lives in ntdll, which every
 * process has loaded, and is looked up rather than linked. */
typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);

static void read_static(void) {
    if (!g_main_tid) g_main_tid = GetCurrentThreadId();

    char vendor[64] = {0}, product[64] = {0};
    read_reg_string("HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemManufacturer", vendor, sizeof(vendor));
    read_reg_string("HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemProductName", product, sizeof(product));
    if (vendor[0] || product[0])
        snprintf(g_host.system, sizeof(g_host.system), "%s%s%s", vendor, (vendor[0] && product[0]) ? " " : "", product);
    else
        snprintf(g_host.system, sizeof(g_host.system), "unknown");

    if (!read_reg_string("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                         g_host.cpu_model, sizeof(g_host.cpu_model)))
        copy_trim(g_host.cpu_model, sizeof(g_host.cpu_model), "unknown CPU");
    g_host.cpu_cores   = count_physical_cores();
    g_host.cpu_threads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    OSVERSIONINFOW vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    RtlGetVersionFn rtl = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char* arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64"
                     : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64" : "unknown";
    if (rtl && rtl(&vi) == 0)
        snprintf(g_host.kernel, sizeof(g_host.kernel), "NT %lu.%lu.%lu %s",
                 (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion,
                 (unsigned long)vi.dwBuildNumber, arch);
    else
        snprintf(g_host.kernel, sizeof(g_host.kernel), "unknown");

    /* ProductName still says "Windows 10" on Windows 11, by Microsoft's own
     * choice; build 22000 is the first Windows 11 build, which is how Microsoft
     * documents telling them apart. Only that word is corrected. */
    char name[64] = {0}, release[32] = {0};
    const char* cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    if (read_reg_string(cv, "ProductName", name, sizeof(name))) {
        if (vi.dwBuildNumber >= 22000 && strncmp(name, "Windows 10", 10) == 0) name[9] = '1';
        read_reg_string(cv, "DisplayVersion", release, sizeof(release));
        snprintf(g_host.distro, sizeof(g_host.distro), "%s%s%s", name, release[0] ? " " : "", release);
    } else {
        copy_trim(g_host.distro, sizeof(g_host.distro), "unknown");
    }

    copy_trim(g_host.gl_vendor,   sizeof(g_host.gl_vendor),   "not yet queried");
    copy_trim(g_host.gl_renderer, sizeof(g_host.gl_renderer), "not yet queried");
    copy_trim(g_host.gl_version,  sizeof(g_host.gl_version),  "-");
    copy_trim(g_host.gl_driver,   sizeof(g_host.gl_driver),   "-");
    copy_trim(g_host.audio_driver, sizeof(g_host.audio_driver), "none");

    g_static_done = true;
}

/* GetThreadDescription is Windows 10 1607 and later, and is what SDL's
 * SDL_CreateThread sets the name with. Looked up rather than linked so the
 * binary still starts on an older kernel; a thread without a name is listed by
 * its id. */
typedef HRESULT (WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR*);

static void thread_name(HANDLE h, DWORD tid, char* out, size_t n) {
    static GetThreadDescriptionFn get_desc = NULL;
    static bool looked = false;
    if (!looked) {
        looked = true;
        get_desc = (GetThreadDescriptionFn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                                 "GetThreadDescription");
    }
    out[0] = '\0';
    PWSTR w = NULL;
    if (get_desc && SUCCEEDED(get_desc(h, &w)) && w) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
        out[n - 1] = '\0';
        LocalFree(w);
    }
    if (!out[0]) snprintf(out, n, "tid %lu", (unsigned long)tid);
}

static unsigned long long filetime_ticks(FILETIME f) {
    return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

void host_info_sample(void) {
    if (!g_static_done) read_static();

    double t = now_seconds();
    double dt = t - g_prev_sample_t;
    if (g_prev_sample_t > 0.0 && dt < 0.5) return;

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        g_host.ram_total_mb = (double)ms.ullTotalPhys / (1024.0 * 1024.0);
        g_host.ram_avail_mb = (double)ms.ullAvailPhys / (1024.0 * 1024.0);
    }
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        g_host.rss_mb = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);

    /* Per-thread CPU, as a share of one core over the sample window. Thread
     * times are in 100 ns units, so one core for one second is 1e7. */
    const double hz = 1e7;
    ThreadTick cur[HOST_MAX_THREADS];
    int cur_count = 0;
    unsigned long long proc_ticks = 0;
    const DWORD pid = GetCurrentProcessId();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        for (BOOL more = Thread32First(snap, &te); more && cur_count < HOST_MAX_THREADS;
             more = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            if (!h) continue;
            FILETIME c, e, k, u;
            if (!GetThreadTimes(h, &c, &e, &k, &u)) { CloseHandle(h); continue; }
            const unsigned long long ticks = filetime_ticks(k) + filetime_ticks(u);

            HostThreadLoad* th = &g_host.threads[cur_count];
            thread_name(h, te.th32ThreadID, th->name, sizeof(th->name));
            CloseHandle(h);

            proc_ticks += ticks;
            cur[cur_count].tid   = te.th32ThreadID;
            cur[cur_count].ticks = ticks;
            th->tid     = (int)te.th32ThreadID;
            th->is_main = (te.th32ThreadID == g_main_tid);

            th->cpu_pct = 0.0;
            if (dt > 0.0) {
                for (int i = 0; i < g_prev_tick_count; i++) {
                    if (g_prev_ticks[i].tid != te.th32ThreadID) continue;
                    th->cpu_pct = 100.0 * (double)(ticks - g_prev_ticks[i].ticks) / (hz * dt);
                    break;
                }
            }
            cur_count++;
        }
        CloseHandle(snap);
    }
    g_host.thread_count = cur_count;

    if (dt > 0.0 && g_prev_proc_ticks)
        g_host.process_cpu_pct = 100.0 * (double)(proc_ticks - g_prev_proc_ticks) / (hz * dt);
    g_prev_proc_ticks = proc_ticks;

    memcpy(g_prev_ticks, cur, sizeof(ThreadTick) * (size_t)cur_count);
    g_prev_tick_count = cur_count;
    g_prev_sample_t = t;
}

#else
/* ------------------------------------------------------------------------- */
/* Linux                                                                      */
/* ------------------------------------------------------------------------- */
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/utsname.h>

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
#endif /* Linux: static fields */

/* ------------------------------------------------------------------------- */
/* Both                                                                       */
/* ------------------------------------------------------------------------- */
static void copy_trim(char* dst, size_t n, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    while (*src == ' ' || *src == '\t' || *src == '"') src++;
    size_t len = strlen(src);
    while (len && (src[len - 1] == '\n' || src[len - 1] == '\r' || src[len - 1] == ' ' ||
                   src[len - 1] == '\t' || src[len - 1] == '"')) len--;
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
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

#ifndef _WIN32
/* ------------------------------------------------------------------------- */
/* Linux: live fields                                                         */
/* ------------------------------------------------------------------------- */

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
#endif /* Linux: live fields */
