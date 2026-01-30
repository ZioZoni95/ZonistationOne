#include "cpu.h"
#include "log.h"
#include "timers.h"

// --- BIOS SYSCALL Handling ---

/**
 * @brief Returns the name of a BIOS A-function for logging.
 */
const char* get_bios_a_function_name(uint32_t func_num) {
    static const char* names[] = {
        "FileOpen", "FileSeek", "FileRead", "FileWrite", "FileClose",          // 00h-04h
        "FileIoctl", "exit", "FileGetDeviceFlag", "FileGetc", "FilePutc",     // 05h-09h
        "todigit", "atof", "strtoul", "strtol", "abs",                        // 0Ah-0Eh
        "labs", "atoi", "atol", "strcat", "index",                            // 0Fh-13h
        "rindex", "strchr", "strrchr", "strcmp", "strncmp",                   // 14h-18h
        "strcpy", "strncpy", "strlen", "memcpy", "memset",                    // 19h-1Dh
        "memmove", "memcmp", "memchr", "rand", "srand",                       // 1Eh-22h
        "qsort", "strtod", "malloc", "free", "lsearch",                       // 23h-27h
        "bsearch", "calloc", "realloc", "InitHeap", "SystemErrorExit",        // 28h-2Ch
        "std_in_getchar", "std_in_testchar", "std_out_putchar", "std_in_gets",// 2Dh-30h
        "std_out_puts", "printf", "SystemErrorUnresolvedException"            // 31h-33h
    };
    if (func_num < sizeof(names) / sizeof(names[0])) {
        return names[func_num];
    }
    return "Unknown_A";
}

/**
 * @brief Returns the name of a BIOS B-function for logging.
 */
const char* get_bios_b_function_name(uint32_t func_num) {
    static const char* names[] = {
        "alloc_kernel_memory", "free_kernel_memory", "init_timer", "get_timer", // 00h-03h
        "enable_timer_irq", "disable_timer_irq", "restart_timer", "DeliverEvent", // 04h-07h
        "OpenEvent", "CloseEvent", "WaitEvent", "TestEvent",                   // 08h-0Bh
        "EnableEvent", "DisableEvent", "OpenTh", "CloseTh",                    // 0Ch-0Fh
        "ChangeTh", "ReturnFromException", "SetDefaultExitFromException",      // 10h-12h
        "SetCustomExitFromException"                                           // 13h
    };
    if (func_num < sizeof(names) / sizeof(names[0])) {
        return names[func_num];
    }
    return "Unknown_B";
}

/**
 * @brief Returns the name of a BIOS C-function for logging.
 */
const char* get_bios_c_function_name(uint32_t func_num) {
    static const char* names[] = {
        "EnqueueTimerAndVblankIrqs", "EnqueueSyscallHandler", "SysEnqIntRP",  // 00h-02h
        "SysDeqIntRP", "get_free_EvCB_slot", "get_free_TCB_slot",            // 03h-05h
        "ExceptionHandler", "InstallExceptionHandlers", "SysInitMemory",      // 06h-08h
        "SysInitKernelVariables", "ChangeClearRCnt", "SystemError",           // 09h-0Bh
        "SetRCnt", "GetRCnt", "StartRCnt", "StopRCnt",                        // 0Ch-0Fh
        "ResetRCnt"                                                            // 10h
    };
    if (func_num < sizeof(names) / sizeof(names[0])) {
        return names[func_num];
    }
    return "Unknown_C";
}

/**
 * @brief Handles specific BIOS A, B, and C function calls.
 * @return Returns true if the syscall was handled, false otherwise.
 */
bool handle_bios_syscall(Cpu* cpu, uint32_t syscall_num) {
    LOG_DEBUG("[BIOS_SYSCALL] Received syscall_num=0x%X", syscall_num);
    switch (syscall_num) {
        case 0x01: // EnterCriticalSection
            cpu->sr &= ~1; // Disable interrupts
            return true;   // Syscall was handled

        case 0x02: // ExitCriticalSection
            cpu->sr |= 1;  // Enable interrupts
            return true;   // Syscall was handled

        case 0x19: // B_clr_event(event)
            // Stub - does nothing, but we acknowledge it as handled.
            return true;   // Syscall was handled
        

        case 0x0C: // SetRCnt (C-function table index)
            // Call timers_handle_setrcnt (to be implemented in timers.c)
            if (cpu->inter && cpu->inter->timers_state.inter) {
                timers_handle_setrcnt(&cpu->inter->timers_state, cpu);
                LOG_INFO("[BIOS] SetRCnt syscall handled");
                return true;
            } else {
                LOG_ERROR("[BIOS] SetRCnt syscall: timers/interconnect not initialized!");
                return false;
            }

        default:
            // We encountered a syscall we don't know how to handle.
            return false;
    }
}