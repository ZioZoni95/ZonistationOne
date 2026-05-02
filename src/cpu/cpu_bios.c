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
    static const char* names[] = {
        "open", "lseek", "read", "write", "close", "ioctl", "exit", "isatty",
        "getc", "putc", "todigit", "atof", "strtoul", "strtol", "abs", "labs",
        "atoi", "atol", "atob", "setjmp", "longjmp", "strcat", "strncat", "strcmp",
        "strncmp", "strcpy", "strncpy", "strlen", "index", "rindex", "strchr",
        "strrchr", "strpbrk", "strspn", "strcspn", "strtok", "strstr", "toupper",
        "tolower", "bcopy", "bzero", "bcmp", "memcpy", "memset", "memmove", "memcmp",
        "memchr", "rand", "srand", "qsort", "strtod", "malloc", "free", "lsearch",
        "bsearch", "calloc", "realloc", "InitHeap", "_exit", "getchar", "putchar",
        "gets", "puts", "printf"
    };
    if (func_num < 0x40) return names[func_num];
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
    
    bios_last_syscall.table = 0;
    bios_last_syscall.func = call;
    bios_last_syscall.name = get_bios_a_function_name(call);
    
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
