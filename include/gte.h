/**
 * gte.h
 * Header file for the PlayStation GTE (Geometry Transformation Engine) emulation.
 * Based on PSX-Spex documentation.
 */
#ifndef GTE_H
#define GTE_H

#include <stdint.h>
#include <stdbool.h>

// --- GTE Data Types ---

// 3D Vector (16-bit components)
typedef struct {
    int16_t x, y, z;
} GteVector3;

// 3D Vector (32-bit components)
typedef struct {
    int32_t x, y, z;
} GteVector3Long;

// 2D Vector (16-bit components)
typedef struct {
    int16_t x, y;
} GteVector2;

// Color (8-bit RGB components)
typedef struct {
    uint8_t r, g, b;
} GteColor;

// Matrix (3x3 rotation matrix)
typedef struct {
    int16_t m[3][3]; // [row][column]
} GteMatrix;

// --- GTE Registers ---
typedef struct {
    // Data Registers (32-bit)
    int32_t data[32]; // GTE Data Registers (V0-V3, R, G, B, OTZ, IR0-IR3, SXY0-SXY2, SXYP, SZ0-SZ3, RGB, LZCS, LZCR)
    
    // Control Registers (32-bit)
    int32_t control[32]; // GTE Control Registers (RT, TR, LIGHT, LCOL, SZ0-SZ3, OFX, OFY, H, DQA, DQB, ZSF3, ZSF4, FLAG)
    
    // Internal state
    bool busy; // GTE busy flag
    uint32_t cycles_remaining; // Cycles until operation completes
    
} Gte;

// --- GTE Function Prototypes ---

/**
 * @brief Initializes the GTE state to power-on defaults.
 * @param gte Pointer to the Gte struct to initialize.
 */
void gte_init(Gte* gte);

/**
 * @brief Executes a GTE instruction.
 * @param gte Pointer to the Gte state.
 * @param instruction The 32-bit GTE instruction to execute.
 * @return Number of cycles the operation takes.
 */
uint32_t gte_execute_instruction(Gte* gte, uint32_t instruction);

/**
 * @brief Reads a GTE data register.
 * @param gte Pointer to the Gte state.
 * @param reg Register number (0-31).
 * @return The 32-bit value of the register.
 */
int32_t gte_read_data_register(Gte* gte, uint32_t reg);

/**
 * @brief Writes a GTE data register.
 * @param gte Pointer to the Gte state.
 * @param reg Register number (0-31).
 * @param value The 32-bit value to write.
 */
void gte_write_data_register(Gte* gte, uint32_t reg, int32_t value);

/**
 * @brief Reads a GTE control register.
 * @param gte Pointer to the Gte state.
 * @param reg Register number (0-31).
 * @return The 32-bit value of the register.
 */
int32_t gte_read_control_register(Gte* gte, uint32_t reg);

/**
 * @brief Writes a GTE control register.
 * @param gte Pointer to the Gte state.
 * @param reg Register number (0-31).
 * @param value The 32-bit value to write.
 */
void gte_write_control_register(Gte* gte, uint32_t reg, int32_t value);

// --- Common GTE Operations ---

// RTPS - Perspective Transformation (Single Point)
void gte_rtps(Gte* gte, uint32_t instruction);

// RTPT - Perspective Transformation (Triangle)
void gte_rtpt(Gte* gte, uint32_t instruction);

// NCLIP - Normal Clipping
void gte_nclip(Gte* gte);

// MVMVA - Matrix-Vector Multiplication
void gte_mvmva(Gte* gte, uint32_t instruction);

// SQR - Square Root
void gte_sqr(Gte* gte, uint32_t instruction);

// OP - Outer Product
void gte_op(Gte* gte, uint32_t instruction);

// DCPL - Depth Cueing
void gte_dcpl(Gte* gte, uint32_t instruction);

// INTPL - Interpolation
void gte_intpl(Gte* gte, uint32_t instruction);

// AVSZ3 - Average Z (3 points)
void gte_avsz3(Gte* gte);

// AVSZ4 - Average Z (4 points)
void gte_avsz4(Gte* gte);

#endif // GTE_H 