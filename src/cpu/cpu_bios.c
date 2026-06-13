#include "cpu.h"
#include "interconnect.h"
#include "bios.h"
#include "log.h"
#include "timers.h"
#include <stdio.h>
#include <string.h>

// =============================================================================
// BIOS Syscall Side-Channel Capture (DuckStation-style LLE)
//
// Called from op_jr BEFORE the CPU jumps to 0xA0 / 0xB0.
// The BIOS still executes normally — we only read arguments to capture TTY
// output.  We do NOT set return values ($v0/$v1).
//
// All character output is funnelled through the same line buffer as the
// EXP2+0x23 DUART path (inter->tty_line_buf), so both sources produce
// consistent [BIOS TTY] output on stderr when a newline is received.
// =============================================================================

// Track the most recent BIOS syscall for context
static struct {
    int table;  // 0 = A0, 1 = B0
    uint32_t func;
    const char* name;
} bios_last_syscall = {0, 0, "none"};

static const char* get_bios_a_function_name(uint32_t func_num) {
    /* nocash PSX A-function table, 0x00-0xBF.
     * 0x73-0xBF: BIOS internal / _96_Cd functions — partially documented. */
    static const char* names[0xC0] = {
        /* 0x00-0x3F: standard C library */
        [0x00]="open",       [0x01]="lseek",      [0x02]="read",        [0x03]="write",
        [0x04]="close",      [0x05]="ioctl",       [0x06]="exit",        [0x07]="isatty",
        [0x08]="getc",       [0x09]="putc",        [0x0A]="todigit",     [0x0B]="atof",
        [0x0C]="strtoul",    [0x0D]="strtol",      [0x0E]="abs",         [0x0F]="labs",
        [0x10]="atoi",       [0x11]="atol",        [0x12]="atob",        [0x13]="setjmp",
        [0x14]="longjmp",    [0x15]="strcat",      [0x16]="strncat",     [0x17]="strcmp",
        [0x18]="strncmp",    [0x19]="strcpy",      [0x1A]="strncpy",     [0x1B]="strlen",
        [0x1C]="index",      [0x1D]="rindex",      [0x1E]="strchr",      [0x1F]="strrchr",
        [0x20]="strpbrk",    [0x21]="strspn",      [0x22]="strcspn",     [0x23]="strtok",
        [0x24]="strstr",     [0x25]="toupper",     [0x26]="tolower",     [0x27]="bcopy",
        [0x28]="bzero",      [0x29]="bcmp",        [0x2A]="memcpy",      [0x2B]="memset",
        [0x2C]="memmove",    [0x2D]="memcmp",      [0x2E]="memchr",      [0x2F]="rand",
        [0x30]="srand",      [0x31]="qsort",       [0x32]="strtod",      [0x33]="malloc",
        [0x34]="free",       [0x35]="lsearch",     [0x36]="bsearch",     [0x37]="calloc",
        [0x38]="realloc",    [0x39]="InitHeap",    [0x3A]="_exit",       [0x3B]="getchar",
        [0x3C]="putchar",    [0x3D]="gets",        [0x3E]="puts",        [0x3F]="printf",
        /* 0x40-0x72: BIOS internals (nocash PSX) */
        [0x40]="SystemErrorUnresolvedException",
        [0x41]="LoadExeHeader",     [0x42]="LoadExeFile",       [0x43]="DoExecute",
        [0x44]="FlushCache",        [0x45]="init_a0_b0_c0_vectors",
        [0x46]="GPU_dw",            [0x47]="gpu_send_dma",
        [0x48]="SendGP1Command",    [0x49]="GPU_cw",
        [0x4A]="GPU_cwp",           [0x4B]="send_gpu_linked_list",
        [0x4C]="gpu_abort_dma",     [0x4D]="GetGPUStatus",      [0x4E]="gpu_sync",
        [0x4F]="SystemError",       [0x50]="SystemError",
        [0x51]="LoadAndExecute",    [0x52]="SystemError",        [0x53]="SystemError",
        [0x54]="CdInit",            [0x55]="_bu_init",           [0x56]="CdRemove",
        [0x57]="unused",            [0x58]="unused",
        [0x59]="unused",            [0x5A]="unused",
        [0x5B]="dev_tty_init",      [0x5C]="dev_tty_open",
        [0x5D]="dev_tty_inout",     [0x5E]="dev_tty_ioctl",
        [0x5F]="dev_cd_open",       [0x60]="dev_cd_read",
        [0x61]="dev_cd_close",      [0x62]="dev_cd_firstfile",
        [0x63]="dev_cd_nextfile",   [0x64]="dev_cd_chdir",
        [0x65]="dev_card_open",     [0x66]="dev_card_read",
        [0x67]="dev_card_write",    [0x68]="dev_card_close",
        [0x69]="dev_card_firstfile",[0x6A]="dev_card_nextfile",
        [0x6B]="dev_card_erase",    [0x6C]="dev_card_undelete",
        [0x6D]="dev_card_format",   [0x6E]="dev_card_rename",
        [0x6F]="dev_card_6f",
        [0x70]="_bu_init",          [0x71]="_96_init",           [0x72]="_96_remove",
        /* 0x73-0x77: undocumented */
        [0x78]="_96_CdSeekL",
        /* 0x79-0x7B: undocumented */
        [0x7C]="_96_CdGetStatus",
        [0x7E]="_96_CdRead",
        [0x7F]="_96_CdReadSector",
        [0x80]="_96_CdSeekP",
        /* 0x81-0xA7: undocumented BIOS internals */
        [0xA8]="BIOS_int_A8",
        /* 0xA9-0xBB: undocumented */
        [0xBC]="BIOS_int_BC",   /* called ~70x/frame from shell rendering loop */
        /* 0xBD-0xBF: undocumented */
    };
    if (func_num < 0xC0 && names[func_num]) return names[func_num];
    return "unknown";
}

static const char* get_bios_b_function_name(uint32_t func_num) {
    static const char* names[] = {
        "alloc_kernel_memory", "free_kernel_memory", "init_timer", "get_timer",
        "enable_timer_irq", "disable_timer_irq", "restart_timer", "DeliverEvent",
        "OpenEvent", "CloseEvent", "WaitEvent", "TestEvent", "EnableEvent", "DisableEvent",
        "OpenTh", "CloseTh", "ChangeTh", "psx_stub (jump_to_00)", "InitPAD2", "StartPAD2",
        "StopPAD2", "PAD_init2", "PAD_dr", "ReturnFromException", "ResetEntryInt",
        "HookEntryInt", "SystemError", "SystemError", "SystemError", "SystemError", 
        "SystemError", "SystemError", "UnDeliverEvent", "SystemError", "SystemError", 
        "SystemError", "psx_stub", "psx_stub", "psx_stub", "psx_stub", "psx_stub", 
        "psx_stub", "SystemError", "SystemError", "psx_stub", "psx_stub", "psx_stub", 
        "psx_stub", "psx_stub", "psx_stub", "open", "lseek", "read", "write", "close", 
        "ioctl", "exit", "isatty", "getc", "putc", "getchar", "putchar", "gets", "puts", 
        "cd", "format", "firstfile2", "nextfile", "rename", "erase", "undelete", 
        "AddDrv", "DelDrv", "PrintInstalledDevices", "InitCARD2", "StartCARD2", "StopCARD2", 
        "_card_info_subfunc", "_card_write", "_card_read", "_new_card", "Krom2RawAdd", 
        "SystemError", "Krom2Offset", "_get_errno", "_get_error", "GetC0Table", "GetB0Table", 
        "_card_chan", "testdevice", "SystemError", "ChangeClearPAD", "_card_status", "_card_wait"
    };
    if (func_num <= 0x5D) return names[func_num];
    return "unknown";
}

static const char* get_bios_c_function_name(uint32_t func_num) {
    /* nocash PSX C-function table (0x00-0x1F) */
    static const char* names[0x1A] = {
        [0x00]="EnqueueTimerAndVblankIrqs",
        [0x01]="EnqueueSyscallHandler",
        [0x02]="SysEnqIntRP",
        [0x03]="SysDeqIntRP",
        [0x04]="get_free_EvCB_slot",
        [0x05]="get_free_TCB_slot",
        [0x06]="ExceptionHandler",
        [0x07]="InstallExceptionHandlers",
        [0x08]="SysInitMemory",
        [0x09]="SysInitKernelVariables",
        [0x0A]="ChangeClearRCnt",
        [0x0B]="SystemError",
        [0x0C]="InitDefInt",
        [0x0D]="SystemError",  [0x0E]="SystemError",  [0x0F]="SystemError",
        [0x10]="SystemError",  [0x11]="SystemError",  [0x12]="SystemError",
        [0x13]="FlushStfIoBuffer",
        [0x14]="unused",  [0x15]="unused",  [0x16]="unused",  [0x17]="unused",
        [0x18]="KernelRedirect",
        [0x19]="PatchAOTable",
    };
    if (func_num < 0x1A && names[func_num]) return names[func_num];
    return "unknown";
}

// Adds one character to the interconnect's TTY line buffer.
// Flushes to stderr as a plain line on newline.
static void tty_add_char(Interconnect* inter, char ch) {
    if (!inter) return;
    uint8_t b = (uint8_t)ch;
    if (ch == '\n' || ch == '\r') {
        if (inter->tty_line_len > 0) {
            inter->tty_line_buf[inter->tty_line_len] = '\0';
            LOG_BIOS_INFO("[TTY] %s",
                    inter->tty_line_buf);
        }
        inter->tty_line_len = 0;    } else if (b >= 0x20 && b < 0x7F) {
        // Printable ASCII only — ignore control chars
        if (inter->tty_line_len < (int)(sizeof(inter->tty_line_buf) - 1))
            inter->tty_line_buf[inter->tty_line_len++] = ch;
    }
}

// Capture write(fd, buf, len) — stdout (fd=1) and stderr (fd=2) only
static void capture_bios_write(Cpu* cpu) {
    uint32_t fd  = cpu->regs[4];  // $a0
    uint32_t buf = cpu->regs[5];  // $a1
    uint32_t len = cpu->regs[6];  // $a2

    if ((fd == 1 || fd == 2) && cpu->inter && len > 0 && len < 0x10000) {
        for (uint32_t i = 0; i < len; i++)
            tty_add_char(cpu->inter,
                         (char)interconnect_load8(cpu->inter, buf + i));
    }
}

// Capture printf(fmt, ...) — output the raw format string from $a0.
// PCSX-style: the format string itself is the "hidden text"
// (format specifiers like %s/%08x appear literally, unsubstituted).
static void capture_bios_printf(Cpu* cpu) {
    uint32_t fmt = cpu->regs[4];  // $a0 = format string pointer

    if (fmt && cpu->inter) {
        for (uint32_t i = 0; i < 256; i++) {
            uint8_t b = interconnect_load8(cpu->inter, fmt + i);
            if (b == 0) break;
            tty_add_char(cpu->inter, (char)b);
        }
    }
}

// Capture putc(c, fd) / putchar(c)
static void capture_bios_putc(Cpu* cpu) {
    uint32_t c  = cpu->regs[4] & 0xFF;  // $a0 = character
    uint32_t fd = cpu->regs[5];          // $a1 = file descriptor (putchar: irrelevant)
    uint32_t fn = cpu->regs[9];          // $t1 = A0/B0 function number

    // putchar (A0:0x3C, B0:0x3D) has no fd; putc (A0:0x09, B0:0x3B) uses fd
    int is_putchar = (fn == 0x3C || fn == 0x3D);
    if (is_putchar || fd == 1 || fd == 2)
        tty_add_char(cpu->inter, (char)c);
}

// Capture puts(str)
static void capture_bios_puts(Cpu* cpu) {
    uint32_t str = cpu->regs[4];  // $a0 = string pointer

    if (str && cpu->inter) {
        for (uint32_t i = 0; i < 512; i++) {
            uint8_t b = interconnect_load8(cpu->inter, str + i);
            if (b == 0) break;
            tty_add_char(cpu->inter, (char)b);
        }
        tty_add_char(cpu->inter, '\n');  // puts() appends newline
    }
}

// Called from op_jr when target == 0xA0
void handle_a0_syscall(Cpu* cpu) {
    uint32_t call = cpu->regs[9]; // $t1
    /* Filter out garbage JR targets misidentified as syscalls (e.g. 0x1E988, 0x1F801800) */
    if (call > 0xFF) return;

    bios_last_syscall.table = 0;
    bios_last_syscall.func = call;
    bios_last_syscall.name = get_bios_a_function_name(call);

    /* Suppress high-frequency shell rendering call (spams log ~70x/frame) */
    if (call == 0xBC) return;
    LOG_CPU_DEBUG("[CPU] A0(%s / 0x%02X)", bios_last_syscall.name, call);

    switch (call) {
        case 0x03: capture_bios_write(cpu);  break;  // write()
        case 0x09:                                    // putc()
        case 0x3C: capture_bios_putc(cpu);   break;  // putchar()
        case 0x3E: capture_bios_puts(cpu);   break;  // puts()
        case 0x3F: capture_bios_printf(cpu); break;  // printf() — outputs raw format string
        default:   break;
    }
    // LLE: Do NOT fake return values — BIOS kernel executes its real getc/putc implementations
    // TTY input is provided via interconnect_tty_input_add() called from main.c keyboard handler
}

// Called from op_jr when target == 0xB0
void handle_b0_syscall(Cpu* cpu) {
    uint32_t call = cpu->regs[9]; // $t1
    
    // Massive hex values mean this is an internal jump/hook, not a syscall
    if (call > 0x5D) return;

    bios_last_syscall.table = 1;
    bios_last_syscall.func = call;
    bios_last_syscall.name = get_bios_b_function_name(call);
    
    // Suppress noise for high-frequency polling calls (GetC = 0x32, B0(0x2C), etc.)
    if (call != 0x32 && call != 0x2C) {
        LOG_BIOS_DEBUG("[BIOS] B0(%s / 0x%02X)", bios_last_syscall.name, call);
    }

    switch (call) {
        case 0x35: capture_bios_write(cpu); break;  // write()
        case 0x3B:                                   // putc()
        case 0x3D: capture_bios_putc(cpu);  break;  // putchar()
        case 0x3F: capture_bios_puts(cpu);  break;  // puts()
        default:   break;
    }
    // Do NOT set cpu->regs[2] — BIOS executes its real function after this hook
}

// Called from op_jr when target == 0xC0
void handle_c0_syscall(Cpu* cpu) {
    uint32_t call = cpu->regs[9]; // $t1
    if (call > 0x1F) return;
    LOG_BIOS_DEBUG("[BIOS] C0(%s / 0x%02X)", get_bios_c_function_name(call), call);
}
