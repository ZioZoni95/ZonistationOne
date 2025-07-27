#ifndef ZONISTATION_COMMON_H
#define ZONISTATION_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define ZS_PLATFORM_WINDOWS
    #define ZS_PLATFORM_STRING "Windows"
    #define ZS_EXPORT __declspec(dllexport)
    #define ZS_IMPORT __declspec(dllimport)
#elif defined(__linux__)
    #define ZS_PLATFORM_LINUX
    #define ZS_PLATFORM_STRING "Linux"
    #define ZS_EXPORT __attribute__((visibility("default")))
    #define ZS_IMPORT
#elif defined(__APPLE__)
    #define ZS_PLATFORM_MACOS
    #define ZS_PLATFORM_STRING "macOS"
    #define ZS_EXPORT __attribute__((visibility("default")))
    #define ZS_IMPORT
#else
    #error "Unsupported platform"
#endif

// Compiler detection
#if defined(__GNUC__)
    #define ZS_COMPILER_GCC
    #define ZS_COMPILER_STRING "GCC"
    #define ZS_FORCE_INLINE __attribute__((always_inline)) inline
    #define ZS_NO_RETURN __attribute__((noreturn))
    #define ZS_UNUSED(x) (void)(x)
#elif defined(_MSC_VER)
    #define ZS_COMPILER_MSVC
    #define ZS_COMPILER_STRING "MSVC"
    #define ZS_FORCE_INLINE __forceinline
    #define ZS_NO_RETURN __declspec(noreturn)
    #define ZS_UNUSED(x) (void)(x)
#else
    #define ZS_COMPILER_STRING "Unknown"
    #define ZS_FORCE_INLINE inline
    #define ZS_NO_RETURN
    #define ZS_UNUSED(x) (void)(x)
#endif

// Type definitions
typedef int8_t   zs_s8;
typedef int16_t  zs_s16;
typedef int32_t  zs_s32;
typedef int64_t  zs_s64;
typedef uint8_t  zs_u8;
typedef uint16_t zs_u16;
typedef uint32_t zs_u32;
typedef uint64_t zs_u64;
typedef size_t   zs_size_t;
typedef bool     zs_bool;

// Boolean constants
#define ZS_TRUE  1
#define ZS_FALSE 0

// PlayStation One specific constants
#define ZS_PSX_CLOCK_FREQUENCY    33868800    // 33.8688 MHz
#define ZS_PSX_RAM_SIZE           (2 * 1024 * 1024)  // 2MB
#define ZS_PSX_BIOS_SIZE          (512 * 1024)       // 512KB
#define ZS_PSX_SCRATCHPAD_SIZE    (1024)             // 1KB
#define ZS_PSX_HARDWARE_REG_SIZE  (8192)             // 8KB

// Memory regions
#define ZS_PSX_RAM_BASE           0x00000000
#define ZS_PSX_BIOS_BASE          0x1FC00000
#define ZS_PSX_SCRATCHPAD_BASE    0x1F800000
#define ZS_PSX_HARDWARE_REG_BASE  0x1F801000

// CPU constants
#define ZS_CPU_REGISTER_COUNT     32
#define ZS_CPU_COPROCESSOR_COUNT  3

// Graphics constants
#define ZS_GPU_VRAM_SIZE          (1024 * 1024)  // 1MB
#define ZS_GPU_MAX_WIDTH          1024
#define ZS_GPU_MAX_HEIGHT         512
#define ZS_GPU_DEFAULT_WIDTH      320
#define ZS_GPU_DEFAULT_HEIGHT     240

// Audio constants
#define ZS_SPU_SAMPLE_RATE        44100
#define ZS_SPU_BUFFER_SIZE        1024
#define ZS_SPU_CHANNEL_COUNT      24

// CD-ROM constants
#define ZS_CDROM_SECTOR_SIZE      2048
#define ZS_CDROM_READ_BUFFER_SIZE (32 * ZS_CDROM_SECTOR_SIZE)

// Error codes
typedef enum {
    ZS_SUCCESS = 0,
    ZS_ERROR_INVALID_PARAMETER = -1,
    ZS_ERROR_OUT_OF_MEMORY = -2,
    ZS_ERROR_FILE_NOT_FOUND = -3,
    ZS_ERROR_FILE_READ = -4,
    ZS_ERROR_FILE_WRITE = -5,
    ZS_ERROR_INVALID_BIOS = -6,
    ZS_ERROR_INVALID_CDROM = -7,
    ZS_ERROR_GPU_INIT = -8,
    ZS_ERROR_SPU_INIT = -9,
    ZS_ERROR_PLUGIN_LOAD = -10,
    ZS_ERROR_UNKNOWN = -999
} zs_error_t;

// Region types
typedef enum {
    ZS_REGION_NTSC = 0,
    ZS_REGION_PAL,
    ZS_REGION_NTSC_J
} zs_region_t;

// CPU execution modes
typedef enum {
    ZS_CPU_MODE_INTERPRETER = 0,
    ZS_CPU_MODE_DYNAMIC_RECOMPILER
} zs_cpu_mode_t;

// Log levels
typedef enum {
    ZS_LOG_LEVEL_ERROR = 0,
    ZS_LOG_LEVEL_WARN,
    ZS_LOG_LEVEL_INFO,
    ZS_LOG_LEVEL_DEBUG,
    ZS_LOG_LEVEL_TRACE
} zs_log_level_t;

// Utility macros
#define ZS_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ZS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ZS_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZS_CLAMP(x, min, max) ZS_MIN(ZS_MAX(x, min), max)

// Bit manipulation macros
#define ZS_BIT(n) (1ULL << (n))
#define ZS_BIT_MASK(n) (ZS_BIT(n) - 1)
#define ZS_GET_BIT(val, bit) (((val) >> (bit)) & 1)
#define ZS_SET_BIT(val, bit) ((val) | ZS_BIT(bit))
#define ZS_CLEAR_BIT(val, bit) ((val) & ~ZS_BIT(bit))
#define ZS_TOGGLE_BIT(val, bit) ((val) ^ ZS_BIT(bit))

// Memory alignment
#define ZS_ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ZS_ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

// Assertions
#ifdef DEBUG
    #include <assert.h>
    #define ZS_ASSERT(expr) assert(expr)
    #define ZS_ASSERT_MSG(expr, msg) assert(expr && msg)
#else
    #define ZS_ASSERT(expr) ((void)0)
    #define ZS_ASSERT_MSG(expr, msg) ((void)0)
#endif

// Logging macros
#define ZS_LOG_ERROR(fmt, ...) zs_log(ZS_LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZS_LOG_WARN(fmt, ...)  zs_log(ZS_LOG_LEVEL_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZS_LOG_INFO(fmt, ...)  zs_log(ZS_LOG_LEVEL_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZS_LOG_DEBUG(fmt, ...) zs_log(ZS_LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define ZS_LOG_TRACE(fmt, ...) zs_log(ZS_LOG_LEVEL_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Function declarations
void zs_log(zs_log_level_t level, const char* file, int line, const char* fmt, ...);
void zs_set_log_level(zs_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_COMMON_H 