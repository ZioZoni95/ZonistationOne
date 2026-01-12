#include "cpu/cpu_types.h"
#include <stdio.h>

/**
 * @brief Returns the name of a BIOS A-function for logging.
 */
const char* get_bios_a_function_name(uint32_t func_num) {
    static const char* names[] = {
        "FileOpen", "FileSeek", "FileRead", "FileWrite",                      // 00h-03h
        "FileClose", "FileIoctl", "exit", "FileGetDeviceFlag",                // 04h-07h
        "FileGetc", "FilePutc", "todigit", "atof",                            // 08h-0Bh
        "strtoul", "strtol", "abs", "labs",                                   // 0Ch-0Fh
        "atoi", "atol", "atob", "SaveState",                                  // 10h-13h
        "RestoreState", "strcat", "strncat", "strcmp",                        // 14h-17h
        "strncmp", "strcpy", "strncpy", "strlen",                             // 18h-1Bh
        "index", "rindex", "strchr", "strrchr",                               // 1Ch-1Fh
        "strpbrk", "strspn", "strcspn", "strtok",                             // 20h-23h
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
