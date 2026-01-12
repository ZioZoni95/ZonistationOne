// SPDX-License-Identifier: MIT
// PlayStation 1 Emulator - Modular BIOS Core Implementation
// Based on DuckStation BIOS System Architecture
// Date: January 7, 2026

#include "bios/bios_core.h"
#include "bios/bios_types.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// MD5 IMPLEMENTATION (Simple, no external dependencies)
// ============================================================================

// MD5 context
typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

// MD5 functions
#define F(x,y,z) ((x & y) | (~x & z))
#define G(x,y,z) ((x & z) | (y & ~z))
#define H(x,y,z) (x ^ y ^ z)
#define I(x,y,z) (y ^ (x | ~z))
#define ROTATE_LEFT(x,n) ((x << n) | (x >> (32-n)))

#define FF(a,b,c,d,x,s,ac) { a += F(b,c,d) + x + ac; a = ROTATE_LEFT(a,s); a += b; }
#define GG(a,b,c,d,x,s,ac) { a += G(b,c,d) + x + ac; a = ROTATE_LEFT(a,s); a += b; }
#define HH(a,b,c,d,x,s,ac) { a += H(b,c,d) + x + ac; a = ROTATE_LEFT(a,s); a += b; }
#define II(a,b,c,d,x,s,ac) { a += I(b,c,d) + x + ac; a = ROTATE_LEFT(a,s); a += b; }

static void MD5Init(MD5_CTX *context) {
    context->count[0] = context->count[1] = 0;
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

static void MD5Transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        x[i] = ((uint32_t)block[j]) | (((uint32_t)block[j+1]) << 8) |
               (((uint32_t)block[j+2]) << 16) | (((uint32_t)block[j+3]) << 24);
    
    // Round 1
    FF(a,b,c,d,x[ 0], 7,0xd76aa478); FF(d,a,b,c,x[ 1],12,0xe8c7b756);
    FF(c,d,a,b,x[ 2],17,0x242070db); FF(b,c,d,a,x[ 3],22,0xc1bdceee);
    FF(a,b,c,d,x[ 4], 7,0xf57c0faf); FF(d,a,b,c,x[ 5],12,0x4787c62a);
    FF(c,d,a,b,x[ 6],17,0xa8304613); FF(b,c,d,a,x[ 7],22,0xfd469501);
    FF(a,b,c,d,x[ 8], 7,0x698098d8); FF(d,a,b,c,x[ 9],12,0x8b44f7af);
    FF(c,d,a,b,x[10],17,0xffff5bb1); FF(b,c,d,a,x[11],22,0x895cd7be);
    FF(a,b,c,d,x[12], 7,0x6b901122); FF(d,a,b,c,x[13],12,0xfd987193);
    FF(c,d,a,b,x[14],17,0xa679438e); FF(b,c,d,a,x[15],22,0x49b40821);
    
    // Round 2
    GG(a,b,c,d,x[ 1], 5,0xf61e2562); GG(d,a,b,c,x[ 6], 9,0xc040b340);
    GG(c,d,a,b,x[11],14,0x265e5a51); GG(b,c,d,a,x[ 0],20,0xe9b6c7aa);
    GG(a,b,c,d,x[ 5], 5,0xd62f105d); GG(d,a,b,c,x[10], 9,0x02441453);
    GG(c,d,a,b,x[15],14,0xd8a1e681); GG(b,c,d,a,x[ 4],20,0xe7d3fbc8);
    GG(a,b,c,d,x[ 9], 5,0x21e1cde6); GG(d,a,b,c,x[14], 9,0xc33707d6);
    GG(c,d,a,b,x[ 3],14,0xf4d50d87); GG(b,c,d,a,x[ 8],20,0x455a14ed);
    GG(a,b,c,d,x[13], 5,0xa9e3e905); GG(d,a,b,c,x[ 2], 9,0xfcefa3f8);
    GG(c,d,a,b,x[ 7],14,0x676f02d9); GG(b,c,d,a,x[12],20,0x8d2a4c8a);
    
    // Round 3
    HH(a,b,c,d,x[ 5], 4,0xfffa3942); HH(d,a,b,c,x[ 8],11,0x8771f681);
    HH(c,d,a,b,x[11],16,0x6d9d6122); HH(b,c,d,a,x[14],23,0xfde5380c);
    HH(a,b,c,d,x[ 1], 4,0xa4beea44); HH(d,a,b,c,x[ 4],11,0x4bdecfa9);
    HH(c,d,a,b,x[ 7],16,0xf6bb4b60); HH(b,c,d,a,x[10],23,0xbebfbc70);
    HH(a,b,c,d,x[13], 4,0x289b7ec6); HH(d,a,b,c,x[ 0],11,0xeaa127fa);
    HH(c,d,a,b,x[ 3],16,0xd4ef3085); HH(b,c,d,a,x[ 6],23,0x04881d05);
    HH(a,b,c,d,x[ 9], 4,0xd9d4d039); HH(d,a,b,c,x[12],11,0xe6db99e5);
    HH(c,d,a,b,x[15],16,0x1fa27cf8); HH(b,c,d,a,x[ 2],23,0xc4ac5665);
    
    // Round 4
    II(a,b,c,d,x[ 0], 6,0xf4292244); II(d,a,b,c,x[ 7],10,0x432aff97);
    II(c,d,a,b,x[14],15,0xab9423a7); II(b,c,d,a,x[ 5],21,0xfc93a039);
    II(a,b,c,d,x[12], 6,0x655b59c3); II(d,a,b,c,x[ 3],10,0x8f0ccc92);
    II(c,d,a,b,x[10],15,0xffeff47d); II(b,c,d,a,x[ 1],21,0x85845dd1);
    II(a,b,c,d,x[ 8], 6,0x6fa87e4f); II(d,a,b,c,x[15],10,0xfe2ce6e0);
    II(c,d,a,b,x[ 6],15,0xa3014314); II(b,c,d,a,x[13],21,0x4e0811a1);
    II(a,b,c,d,x[ 4], 6,0xf7537e82); II(d,a,b,c,x[11],10,0xbd3af235);
    II(c,d,a,b,x[ 2],15,0x2ad7d2bb); II(b,c,d,a,x[ 9],21,0xeb86d391);
    
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void MD5Update(MD5_CTX *context, const uint8_t *input, size_t inputLen) {
    size_t i, index, partLen;
    index = (context->count[0] >> 3) & 0x3F;
    
    if ((context->count[0] += (inputLen << 3)) < (inputLen << 3))
        context->count[1]++;
    context->count[1] += (inputLen >> 29);
    
    partLen = 64 - index;
    
    if (inputLen >= partLen) {
        memcpy(&context->buffer[index], input, partLen);
        MD5Transform(context->state, context->buffer);
        
        for (i = partLen; i + 63 < inputLen; i += 64)
            MD5Transform(context->state, &input[i]);
        index = 0;
    } else {
        i = 0;
    }
    
    memcpy(&context->buffer[index], &input[i], inputLen - i);
}

static void MD5Final(uint8_t digest[16], MD5_CTX *context) {
    uint8_t bits[8];
    uint32_t index, padLen;
    
    for (int i = 0, j = 0; i < 2; i++, j += 4) {
        bits[j] = context->count[i] & 0xff;
        bits[j+1] = (context->count[i] >> 8) & 0xff;
        bits[j+2] = (context->count[i] >> 16) & 0xff;
        bits[j+3] = (context->count[i] >> 24) & 0xff;
    }
    
    index = (context->count[0] >> 3) & 0x3f;
    padLen = (index < 56) ? (56 - index) : (120 - index);
    
    uint8_t PADDING[64] = {0x80};
    MD5Update(context, PADDING, padLen);
    MD5Update(context, bits, 8);
    
    for (int i = 0, j = 0; i < 4; i++, j += 4) {
        digest[j] = context->state[i] & 0xff;
        digest[j+1] = (context->state[i] >> 8) & 0xff;
        digest[j+2] = (context->state[i] >> 16) & 0xff;
        digest[j+3] = (context->state[i] >> 24) & 0xff;
    }
}

// ============================================================================
// KNOWN BIOS DATABASE (Top 20 Most Common BIOSes)
// ============================================================================

static const BiosImageInfo s_known_bios_database[BIOS_KNOWN_COUNT] = {
    // NTSC-U (North America) - Priority: 5-10
    {"SCPH-1001, 5003, DTL-H1201, H3001 (v2.2 12-04-95 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 5,
        BIOS_HASH(0x92,0x4e,0x39,0x2e,0xd0,0x55,0x58,0xff,0xdb,0x11,0x54,0x08,0xc2,0x63,0xdc,0xcf)},
    
    {"SCPH-5501, 5503, 7003 (v3.0 11-18-96 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 5,
        BIOS_HASH(0x49,0x0f,0x66,0x6e,0x1a,0xfb,0x15,0xb7,0x36,0x2b,0x40,0x6e,0xd1,0xce,0xa2,0x46)},
    
    {"SCPH-7001, 7501, 7503, 9001, 9003, 9903 (v4.1 12-16-97 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x1e,0x68,0xc2,0x31,0xd0,0x89,0x6b,0x7e,0xad,0xca,0xd1,0xd7,0xd8,0xe7,0x61,0x29)},
    
    {"SCPH-101 (v4.5 05-25-00 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x6e,0x37,0x35,0xff,0x4c,0x7d,0xc8,0x99,0xee,0x98,0x98,0x13,0x85,0xf6,0xf3,0xd0)},
    
    {"SCPH-1001, DTL-H1001 (v2.0 05-07-95 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0xdc,0x2b,0x9b,0xf8,0xda,0x62,0xec,0x93,0xe8,0x68,0xcf,0xd2,0x9f,0x0d,0x06,0x7d)},
    
    // NTSC-J (Japan) - Priority: 5-20
    {"SCPH-5000, DTL-H1200, H3000 (v2.2 12-04-95 J)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 5,
        BIOS_HASH(0x57,0xa0,0x63,0x03,0xdf,0xa9,0xcf,0x93,0x51,0x22,0x2d,0xfc,0xbb,0x4a,0x29,0xd9)},
    
    {"SCPH-5500 (v3.0 09-09-96 J)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 5,
        BIOS_HASH(0x8d,0xd7,0xd5,0x29,0x6a,0x65,0x0f,0xac,0x73,0x19,0xbc,0xe6,0x65,0xa6,0xa5,0x3c)},
    
    {"SCPH-7000, 7500, 9000 (v4.0 08-18-97 J)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x8e,0x4c,0x14,0xf5,0x67,0x74,0x5e,0xff,0x2f,0x04,0x08,0xc8,0x12,0x9f,0x72,0xa6)},
    
    {"SCPH-1000, DTL-H1000 (v1.0)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 50,
        BIOS_HASH(0x23,0x96,0x65,0xb1,0xa3,0xda,0xde,0x1b,0x5a,0x52,0xc0,0x63,0x38,0x01,0x10,0x44)},
    
    {"SCPH-3000, DTL-H1000H (v1.1 01-22-95)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x84,0x95,0x15,0x93,0x91,0x61,0xe6,0x2f,0x6b,0x86,0x6f,0x68,0x53,0x00,0x67,0x80)},
    
    // PAL (Europe) - Priority: 5-20
    {"SCPH-5502, 5552 (v3.0 01-06-97 E)", BIOS_REGION_PAL, false, BIOS_FASTBOOT_TYPE1, 5,
        BIOS_HASH(0x32,0x73,0x6f,0x17,0x07,0x9d,0x0b,0x2b,0x70,0x24,0x40,0x7c,0x39,0xbd,0x30,0x50)},
    
    {"SCPH-7002, 7502, 9002 (v4.1 12-16-97 E)", BIOS_REGION_PAL, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0xb9,0xd9,0xa0,0x28,0x6c,0x33,0xdc,0x6b,0x72,0x37,0xbb,0x13,0xcd,0x46,0xfd,0xee)},
    
    {"SCPH-102 (v4.5 05-25-00 E)", BIOS_REGION_PAL, true, BIOS_FASTBOOT_TYPE1, 20,
        BIOS_HASH(0xde,0x93,0xca,0xec,0x13,0xd1,0xa1,0x41,0xa4,0x0a,0x79,0xf5,0xc8,0x61,0x68,0xd6)},
    
    {"SCPH-1002, DTL-H1002 (v2.0 05-10-95 E)", BIOS_REGION_PAL, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x54,0x84,0x7e,0x69,0x34,0x05,0xff,0xeb,0x03,0x59,0xc6,0x28,0x74,0x34,0xcb,0xef)},
    
    {"SCPH-1002, DTL-H1102 (v2.1 07-17-95 E)", BIOS_REGION_PAL, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x41,0x7b,0x34,0x70,0x63,0x19,0xda,0x7c,0xf0,0x01,0xe7,0x6e,0x40,0x13,0x6c,0x23)},
    
    {"SCPH-1002, DTL-H1202, H3002 (v2.2 12-04-95 E)", BIOS_REGION_PAL, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0xe2,0x11,0x0b,0x8a,0x2b,0x97,0xa8,0xe0,0xb8,0x57,0xa4,0x5d,0x32,0xf7,0xe1,0x87)},
    
    // Additional common BIOSes
    {"SCPH-1001, DTL-H1101 (v2.1 07-17-95 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0xda,0x27,0xe8,0xb6,0xda,0xb2,0x42,0xd8,0xf9,0x1a,0x9b,0x25,0xd8,0x0c,0x63,0xb8)},
    
    {"SCPH-3500 (v2.1 07-17-95 J)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0xcb,0xa7,0x33,0xce,0xef,0xf5,0xae,0xf5,0xc3,0x22,0x54,0xf1,0xd6,0x17,0xfa,0x62)},
    
    {"DTL-H1100 (v2.2 03-06-96 D)", BIOS_REGION_NTSC_J, true, BIOS_FASTBOOT_TYPE1, 20,
        BIOS_HASH(0xca,0x5c,0xfc,0x32,0x1f,0x91,0x67,0x56,0xe3,0xf0,0xef,0xfb,0xfa,0xeb,0xa1,0x3b)},
    
    {"SCPH-101 (v4.4 03-24-00 A)", BIOS_REGION_NTSC_U, false, BIOS_FASTBOOT_TYPE1, 10,
        BIOS_HASH(0x9a,0x09,0xab,0x7e,0x49,0xb4,0x22,0xc0,0x07,0xe6,0xd5,0x4d,0x7c,0x49,0xb9,0x65)},
};

// ============================================================================
// BIOS FUNCTION NAME TABLES
// ============================================================================

static const char* s_bios_function_table_a[] = {
    /* 0x00 */ "FileOpen", "FileSeek", "FileRead", "FileWrite",
    /* 0x04 */ "FileClose", "ioabort", "FileGets", "FileGetc",
    /* 0x08 */ "FilePutc", "Unknown_A", "todigit", "atof",
    /* 0x0C */ "strtoul", "strtol", "abs", "labs",
    /* 0x10 */ "atoi", "atol", "atob", "Unknown_A",
    /* 0x14 */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x18 */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x1C */ "index", "rindex", "strchr", "strrchr",
    /* 0x20 */ "strpbrk", "strspn", "strcspn", "strtok",
    /* 0x24 */ "strstr", "toupper", "tolower", "bcopy",
    /* 0x28 */ "bzero", "bcmp", "memcpy", "memset",
    /* 0x2C */ "memmove", "memcmp", "memchr", "rand",
    /* 0x30 */ "srand", "qsort", "strtod", "malloc",
    /* 0x34 */ "free", "lsearch", "bsearch", "calloc",
    /* 0x38 */ "realloc", "InitHeap", "_exit", "getchar",
    /* 0x3C */ "putchar", "gets", "puts", "printf",
    /* 0x40 */ "SystemErrorUnresolvedException", "LoadTest", "Load", "Exec",
    /* 0x44 */ "FlushCache", "InstallInterruptHandler", "GPU_dw", "GPU_cw",
    /* 0x48 */ "SendGPU", "GPU_cwp", "GPU_cwb", "SendPrimitive",
    /* 0x4C */ "GetGPUStatus", "GPU_sync", "Unknown_A", "Unknown_A",
    /* 0x50 */ "LoadExeHeader", "LoadExeFile", "DoExecute", "FlushCache",
    /* 0x54 */ "_bu_init", "_96_init", "_96_remove", "Unknown_A",
    /* 0x58 */ "Unknown_A", "Unknown_A", "Unknown_A", "dev_tty_init",
    /* 0x5C */ "dev_tty_open", "dev_tty_in_out", "dev_tty_ioctl", "dev_cd_open",
    /* 0x60 */ "dev_cd_read", "dev_cd_close", "dev_cd_firstfile", "dev_cd_nextfile",
    /* 0x64 */ "dev_cd_chdir", "dev_card_open", "dev_card_read", "dev_card_write",
    /* 0x68 */ "dev_card_close", "dev_card_firstfile", "dev_card_nextfile", "dev_card_erase",
    /* 0x6C */ "dev_card_undelete", "dev_card_format", "dev_card_rename", "Unknown_A",
    /* 0x70 */ "_bu_init", "_96_init", "_96_remove", "Unknown_A",
    /* 0x74 */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x78 */ "_96_CdSeekL", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x7C */ "CdAsyncSeekL", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x80 */ "CdAsyncGetStatus", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x84 */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x88 */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x8C */ "Unknown_A", "Unknown_A", "Unknown_A", "Unknown_A",
    /* 0x90 */ "CdromIoIrqFunc1", "CdromDmaIrqFunc1", "CdromIoIrqFunc2", "CdromDmaIrqFunc2",
    /* 0x94 */ "CdromGetInt5errCode", "CdInitSubFunc", "AddCDROMDevice", "AddMemCardDevice",
    /* 0x98 */ "AddDuartTtyDevice", "add_nullcon_driver", "Unknown_A", "Unknown_A",
    /* 0x9C */ "SetConf", "GetConf", "SetCdromIrqAutoAbort", "SetMemSize",
    /* 0xA0 */ "WarmBoot", "SystemErrorBootOrDiskFailure", "EnqueueCdIntr", "DequeueCdIntr",
    /* 0xA4 */ "CdGetLbn", "CdReadSector", "CdGetStatus", "bufs_cb_0",
    /* 0xA8 */ "bufs_cb_1", "bufs_cb_2", "bufs_cb_3", "_card_info",
    /* 0xAC */ "_card_async_load_directory", "Unknown_A", "_card_load", "_card_auto",
    /* 0xB0 */ "Unknown_A", "Unknown_A", "ioabort_raw", "Unknown_A",
    /* 0xB4 */ "GetSystemInfo", "Unknown_A", "Unknown_A", "Unknown_A",
};

static const char* s_bios_function_table_b[] = {
    /* 0x00 */ "alloc_kernel_memory", "free_kernel_memory", "init_timer", "get_timer",
    /* 0x04 */ "enable_timer_irq", "disable_timer_irq", "restart_timer", "Unknown_B",
    /* 0x08 */ "OpenEvent", "CloseEvent", "WaitEvent", "TestEvent",
    /* 0x0C */ "EnableEvent", "DisableEvent", "OpenThread", "CloseThread",
    /* 0x10 */ "ChangeThread", "Unknown_B", "InitPad", "StartPad",
    /* 0x14 */ "StopPad", "OutdatedPadInitAndStart", "OutdatedPadGetButtons", "ReturnFromException",
    /* 0x18 */ "SetDefaultExitFromException", "SetCustomExitFromException", "Unknown_B", "Unknown_B",
    /* 0x1C */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x20 */ "UnDeliverEvent", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x24 */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x28 */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x2C */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x30 */ "Unknown_B", "Unknown_B", "open", "lseek",
    /* 0x34 */ "read", "write", "close", "ioctl",
    /* 0x38 */ "exit", "sys_a0_3c", "getc", "putc",
    /* 0x3C */ "getchar", "putchar", "gets", "puts",
    /* 0x40 */ "cd", "format", "firstfile", "nextfile",
    /* 0x44 */ "rename", "erase", "undelete", "AddDevice",
    /* 0x48 */ "RemoveDevice", "PrintInstalledDevices", "Unknown_B", "Unknown_B",
    /* 0x4C */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x50 */ "InitCard", "StartCard", "StopCard", "_card_info_subfunc",
    /* 0x54 */ "write_card_sector", "read_card_sector", "allow_new_card", "Unknown_B",
    /* 0x58 */ "Unknown_B", "Unknown_B", "Unknown_B", "Unknown_B",
    /* 0x5C */ "Krom2RawAdd", "Krom2Offset", "GetLastError", "GetLastFileError",
    /* 0x60 */ "GetC0Table", "GetB0Table", "get_bu_callback_port", "testdevice",
};

static const char* s_bios_function_table_c[] = {
    /* 0x00 */ "EnqueueTimerAndVblankIrqs", "EnqueueSyscallHandler", "SysEnqIntRP", "SysDeqIntRP",
    /* 0x04 */ "get_free_EvCB_slot", "get_free_TCB_slot", "ExceptionHandler", "InstallExceptionHandlers",
    /* 0x08 */ "SysInitMemory", "SysInitKernelVariables", "ChangeClearRCnt", "SystemError",
    /* 0x0C */ "InitDefInt", "SetIrqAutoAck", "Unknown_C", "Unknown_C",
    /* 0x10 */ "Unknown_C", "Unknown_C", "InstallDevices", "FlushStdInOutPut",
    /* 0x14 */ "Unknown_C", "tty_cdevinput", "tty_cdevscan", "tty_circgetc",
    /* 0x18 */ "tty_circputc", "ioabort", "set_card_find_mode", "KernelRedirect",
    /* 0x1C */ "AdjustA0Table", "get_card_find_mode", "Unknown_C", "Unknown_C",
};

// ============================================================================
// CORE API IMPLEMENTATION
// ============================================================================

void bios_init(BiosState* bios) {
    memset(bios, 0, sizeof(BiosState));
    bios->info = NULL;
    bios->region = BIOS_REGION_AUTO;
    bios->verified = false;
    bios->loaded = false;
}

void bios_compute_hash(BiosState* bios) {
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, bios->data, BIOS_SIZE);
    MD5Final(bios->hash, &ctx);
}

const BiosImageInfo* bios_find_info_by_hash(const uint8_t hash[BIOS_HASH_SIZE]) {
    for (int i = 0; i < BIOS_KNOWN_COUNT; i++) {
        if (bios_hash_equal(hash, s_known_bios_database[i].hash)) {
            return &s_known_bios_database[i];
        }
    }
    return NULL;
}

bool bios_load(BiosState* bios, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        LOG_BIOS_ERROR("[BIOS] Failed to open file: %s", path);
        return false;
    }
    
    size_t bytes_read = fread(bios->data, 1, BIOS_SIZE, file);
    fclose(file);
    
    if (bytes_read != BIOS_SIZE) {
        LOG_BIOS_ERROR("[BIOS] Size mismatch: Read %zu bytes, expected %d", bytes_read, BIOS_SIZE);
        return false;
    }
    
    // Compute MD5 hash
    bios_compute_hash(bios);
    
    // Try to find matching BIOS info
    bios->info = bios_find_info_by_hash(bios->hash);
    
    if (bios->info) {
        bios->verified = true;
        bios->region = bios->info->region;
        LOG_BIOS_WARN("[BIOS] Loaded: %s - %s [VERIFIED]", 
                      bios->info->description, 
                      bios_region_name(bios->region));
    } else {
        bios->verified = false;
        bios->region = BIOS_REGION_AUTO;
        
        char hash_str[33];
        bios_hash_to_string(bios->hash, hash_str);
        LOG_BIOS_WARN("[BIOS] Loaded: Unknown BIOS (MD5: %s) [UNVERIFIED]", hash_str);
    }
    
    bios->loaded = true;
    return true;
}

bool bios_load_and_verify(BiosState* bios, const char* path) {
    return bios_load(bios, path);
}

// ============================================================================
// MEMORY ACCESS
// ============================================================================

uint32_t bios_load32(const BiosState* bios, uint32_t offset) {
    if (!bios_offset_valid(offset, 4)) {
        LOG_BIOS_ERROR("[BIOS] load32 out of bounds: offset=0x%X", offset);
        return 0;
    }
    
    uint32_t b0 = bios->data[offset + 0];
    uint32_t b1 = bios->data[offset + 1];
    uint32_t b2 = bios->data[offset + 2];
    uint32_t b3 = bios->data[offset + 3];
    
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

uint16_t bios_load16(const BiosState* bios, uint32_t offset) {
    if (!bios_offset_valid(offset, 2)) {
        LOG_BIOS_ERROR("[BIOS] load16 out of bounds: offset=0x%X", offset);
        return 0;
    }
    
    uint8_t b0 = bios->data[offset];
    uint8_t b1 = bios->data[offset + 1];
    return (uint16_t)(b0 | (b1 << 8));
}

uint8_t bios_load8(const BiosState* bios, uint32_t offset) {
    if (!bios_offset_valid(offset, 1)) {
        LOG_BIOS_ERROR("[BIOS] load8 out of bounds: offset=0x%X", offset);
        return 0;
    }
    return bios->data[offset];
}

// ============================================================================
// BIOS INFO & VERIFICATION
// ============================================================================

const char* bios_get_description(const BiosState* bios) {
    if (bios->info) {
        return bios->info->description;
    }
    return "Unknown BIOS";
}

bool bios_is_verified(const BiosState* bios) {
    return bios->verified;
}

BiosRegion bios_get_region(const BiosState* bios) {
    return bios->region;
}

bool bios_supports_fastboot(const BiosState* bios) {
    if (!bios->info) return false;
    return bios->info->fastboot_patch != BIOS_FASTBOOT_UNSUPPORTED;
}

// ============================================================================
// BIOS FUNCTION CALL SYSTEM
// ============================================================================

const char* bios_get_function_name(uint8_t table, uint8_t function) {
    switch (table) {
        case BIOS_TABLE_A:
            if (function < sizeof(s_bios_function_table_a) / sizeof(char*)) {
                return s_bios_function_table_a[function];
            }
            break;
        case BIOS_TABLE_B:
            if (function < sizeof(s_bios_function_table_b) / sizeof(char*)) {
                return s_bios_function_table_b[function];
            }
            break;
        case BIOS_TABLE_C:
            if (function < sizeof(s_bios_function_table_c) / sizeof(char*)) {
                return s_bios_function_table_c[function];
            }
            break;
    }
    return "Unknown";
}

const BiosFunctionInfo* bios_decode_function(uint8_t table, uint8_t function) {
    static BiosFunctionInfo info;
    info.table = table;
    info.function = function;
    info.name = bios_get_function_name(table, function);
    return &info;
}

void bios_log_function_call(uint32_t pc, uint8_t table, uint8_t function, uint32_t ra) {
    const char* func_name = bios_get_function_name(table, function);
    const char* table_name = bios_table_name(table);
    
    LOG_BIOS_DEBUG("[BIOS] @PC=0x%08X: %s(%02Xh) = %s() [RA=0x%08X]", 
                   pc, table_name, function, func_name, ra);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void bios_hash_to_string(const uint8_t hash[BIOS_HASH_SIZE], char buffer[33]) {
    for (int i = 0; i < BIOS_HASH_SIZE; i++) {
        snprintf(buffer + (i * 2), 3, "%02x", hash[i]);
    }
    buffer[32] = '\0';
}

void bios_get_stats(const BiosState* bios, 
                    const char** description,
                    const char** region,
                    bool* verified,
                    char hash_str[33]) {
    if (description) *description = bios_get_description(bios);
    if (region) *region = bios_region_name(bios->region);
    if (verified) *verified = bios->verified;
    if (hash_str) bios_hash_to_string(bios->hash, hash_str);
}
