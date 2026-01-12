#include "cpu/cpu_disasm.h"
#include "cpu/cpu_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Static Tables for Disassembly
// ============================================================

static const char* s_base_table[64] = {
  "",                       // 0
  "UNKNOWN",                // 1
  "j $jt",                  // 2
  "jal $jt",                // 3
  "beq $rs, $rt, $rel",     // 4
  "bne $rs, $rt, $rel",     // 5
  "blez $rs, $rel",         // 6
  "bgtz $rs, $rel",         // 7
  "addi $rt, $rs, $imm",    // 8
  "addiu $rt, $rs, $imm",   // 9
  "slti $rt, $rs, $imm",    // 10
  "sltiu $rt, $rs, $immu",  // 11
  "andi $rt, $rs, $immx",   // 12
  "ori $rt, $rs, $immx",    // 13
  "xori $rt, $rs, $immx",   // 14
  "lui $rt, $immx",         // 15
  "UNKNOWN",                // 16
  "UNKNOWN",                // 17
  "UNKNOWN",                // 18
  "UNKNOWN",                // 19
  "UNKNOWN",                // 20
  "UNKNOWN",                // 21
  "UNKNOWN",                // 22
  "UNKNOWN",                // 23
  "UNKNOWN",                // 24
  "UNKNOWN",                // 25
  "UNKNOWN",                // 26
  "UNKNOWN",                // 27
  "UNKNOWN",                // 28
  "UNKNOWN",                // 29
  "UNKNOWN",                // 30
  "UNKNOWN",                // 31
  "lb $rt, $offsetrs",      // 32
  "lh $rt, $offsetrs",      // 33
  "lwl $rt, $offsetrs",     // 34
  "lw $rt, $offsetrs",      // 35
  "lbu $rt, $offsetrs",     // 36
  "lhu $rt, $offsetrs",     // 37
  "lwr $rt, $offsetrs",     // 38
  "UNKNOWN",                // 39
  "sb $rt, $offsetrs",      // 40
  "sh $rt, $offsetrs",      // 41
  "swl $rt, $offsetrs",     // 42
  "sw $rt, $offsetrs",      // 43
  "UNKNOWN",                // 44
  "UNKNOWN",                // 45
  "swr $rt, $offsetrs",     // 46
  "UNKNOWN",                // 47
  "lwc0 $coprt, $offsetrs", // 48
  "lwc1 $coprt, $offsetrs", // 49
  "lwc2 $coprt, $offsetrs", // 50
  "lwc3 $coprt, $offsetrs", // 51
  "UNKNOWN",                // 52
  "UNKNOWN",                // 53
  "UNKNOWN",                // 54
  "UNKNOWN",                // 55
  "swc0 $coprt, $offsetrs", // 56
  "swc1 $coprt, $offsetrs", // 57
  "swc2 $coprt, $offsetrs", // 58
  "swc3 $coprt, $offsetrs", // 59
  "UNKNOWN",                // 60
  "UNKNOWN",                // 61
  "UNKNOWN",                // 62
  "UNKNOWN"                 // 63
};

static const char* s_special_table[64] = {
  "sll $rd, $rt, $shamt", // 0
  "UNKNOWN",              // 1
  "srl $rd, $rt, $shamt", // 2
  "sra $rd, $rt, $shamt", // 3
  "sllv $rd, $rt, $rs",   // 4
  "UNKNOWN",              // 5
  "srlv $rd, $rt, $rs",   // 6
  "srav $rd, $rt, $rs",   // 7
  "jr $rs",               // 8
  "jalr $rd, $rs",        // 9
  "UNKNOWN",              // 10
  "UNKNOWN",              // 11
  "syscall",              // 12
  "break",                // 13
  "UNKNOWN",              // 14
  "UNKNOWN",              // 15
  "mfhi $rd",             // 16
  "mthi $rs",             // 17
  "mflo $rd",             // 18
  "mtlo $rs",             // 19
  "UNKNOWN",              // 20
  "UNKNOWN",              // 21
  "UNKNOWN",              // 22
  "UNKNOWN",              // 23
  "mult $rs, $rt",        // 24
  "multu $rs, $rt",       // 25
  "div $rs, $rt",         // 26
  "divu $rs, $rt",        // 27
  "UNKNOWN",              // 28
  "UNKNOWN",              // 29
  "UNKNOWN",              // 30
  "UNKNOWN",              // 31
  "add $rd, $rs, $rt",    // 32
  "addu $rd, $rs, $rt",   // 33
  "sub $rd, $rs, $rt",    // 34
  "subu $rd, $rs, $rt",   // 35
  "and $rd, $rs, $rt",    // 36
  "or $rd, $rs, $rt",     // 37
  "xor $rd, $rs, $rt",    // 38
  "nor $rd, $rs, $rt",    // 39
  "UNKNOWN",              // 40
  "UNKNOWN",              // 41
  "slt $rd, $rs, $rt",    // 42
  "sltu $rd, $rs, $rt",   // 43
  "UNKNOWN",              // 44
  "UNKNOWN",              // 45
  "UNKNOWN",              // 46
  "UNKNOWN",              // 47
  "UNKNOWN",              // 48
  "UNKNOWN",              // 49
  "UNKNOWN",              // 50
  "UNKNOWN",              // 51
  "UNKNOWN",              // 52
  "UNKNOWN",              // 53
  "UNKNOWN",              // 54
  "UNKNOWN",              // 55
  "UNKNOWN",              // 56
  "UNKNOWN",              // 57
  "UNKNOWN",              // 58
  "UNKNOWN",              // 59
  "UNKNOWN",              // 60
  "UNKNOWN",              // 61
  "UNKNOWN",              // 62
  "UNKNOWN"               // 63
};

static const char* s_cop0_table[32] = {
  "UNKNOWN",    // 0
  "tlbr",       // 1
  "tlbwi",      // 2
  "UNKNOWN",    // 3
  "tlbwr",      // 4
  "UNKNOWN",    // 5
  "UNKNOWN",    // 6
  "UNKNOWN",    // 7
  "tlbp",       // 8
  "UNKNOWN",    // 9
  "UNKNOWN",    // 10
  "UNKNOWN",    // 11
  "UNKNOWN",    // 12
  "UNKNOWN",    // 13
  "UNKNOWN",    // 14
  "UNKNOWN",    // 15
  "rfe",        // 16
  "UNKNOWN",    // 17
  "UNKNOWN",    // 18
  "UNKNOWN",    // 19
  "UNKNOWN",    // 20
  "UNKNOWN",    // 21
  "UNKNOWN",    // 22
  "UNKNOWN",    // 23
  "UNKNOWN",    // 24
  "UNKNOWN",    // 25
  "UNKNOWN",    // 26
  "UNKNOWN",    // 27
  "UNKNOWN",    // 28
  "UNKNOWN",    // 29
  "UNKNOWN",    // 30
  "UNKNOWN"     // 31
};

static const char* s_reg_names[32] = {
  "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
  "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
  "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
  "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

static const char* s_cop_names[4] = {"cop0", "cop1", "cop2", "cop3"};

// ============================================================
// Helper Functions
// ============================================================

static void format_instruction(char* dest, size_t dest_size, const Instruction* inst, uint32_t pc, const char* format) {
  char temp[256];
  size_t pos = 0;

  while (*format && pos < sizeof(temp) - 1) {
    if (*format == '$') {
      format++;
      if (strncmp(format, "rs", 2) == 0) {
        const char* reg_name = s_reg_names[inst->i.rs];
        size_t len = strlen(reg_name);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, reg_name);
          pos += len;
        }
        format += 2;
      } else if (strncmp(format, "rt", 2) == 0) {
        const char* reg_name = s_reg_names[inst->i.rt];
        size_t len = strlen(reg_name);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, reg_name);
          pos += len;
        }
        format += 2;
      } else if (strncmp(format, "rd", 2) == 0) {
        const char* reg_name = s_reg_names[inst->r.rd];
        size_t len = strlen(reg_name);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, reg_name);
          pos += len;
        }
        format += 2;
      } else if (strncmp(format, "imm", 3) == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int16_t)inst->i.imm);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 3;
      } else if (strncmp(format, "immu", 4) == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%04X", inst->i.imm);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 4;
      } else if (strncmp(format, "immx", 4) == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%04X", inst->i.imm);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 4;
      } else if (strncmp(format, "jt", 2) == 0) {
        uint32_t target = (pc & 0xF0000000) | (inst->j.target << 2);
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", target);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 2;
      } else if (strncmp(format, "rel", 3) == 0) {
        int32_t offset = (int16_t)inst->i.imm << 2;
        uint32_t target = pc + 4 + offset;
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", target);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 3;
      } else if (strncmp(format, "shamt", 5) == 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", inst->r.shamt);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 5;
      } else if (strncmp(format, "offsetrs", 8) == 0) {
        char buf[32];
        int16_t offset = (int16_t)inst->i.imm;
        const char* reg_name = s_reg_names[inst->i.rs];
        if (offset == 0) {
          snprintf(buf, sizeof(buf), "(%s)", reg_name);
        } else {
          snprintf(buf, sizeof(buf), "%d(%s)", offset, reg_name);
        }
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 8;
      } else if (strncmp(format, "coprt", 5) == 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", inst->i.rt);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 5;
      } else if (strncmp(format, "coprd", 5) == 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", inst->r.rd);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 5;
      } else if (strncmp(format, "coprdc", 6) == 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", inst->r.rd);
        size_t len = strlen(buf);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, buf);
          pos += len;
        }
        format += 6;
      } else if (strncmp(format, "cop", 3) == 0) {
        const char* cop_name = s_cop_names[inst->cop.cop_n];
        size_t len = strlen(cop_name);
        if (pos + len < sizeof(temp) - 1) {
          strcpy(temp + pos, cop_name);
          pos += len;
        }
        format += 3;
      } else {
        temp[pos++] = '$';
      }
    } else {
      temp[pos++] = *format++;
    }
  }
  temp[pos] = '\0';

  // Copy to destination buffer
  strncpy(dest, temp, dest_size - 1);
  dest[dest_size - 1] = '\0';
}

// ============================================================
// Public API Implementation
// ============================================================

void cpu_disassemble_instruction(char* dest, size_t dest_size, uint32_t pc, uint32_t bits) {
  Instruction inst = {.bits = bits};

  switch (inst.op) {
    case OPCODE_SPECIAL:
      if (s_special_table[inst.r.funct]) {
        format_instruction(dest, dest_size, &inst, pc, s_special_table[inst.r.funct]);
      } else {
        snprintf(dest, dest_size, "UNKNOWN");
      }
      break;

    case OPCODE_COP0:
    case OPCODE_COP1:
    case OPCODE_COP2:
    case OPCODE_COP3: {
      uint32_t cop_op = (bits >> 21) & 0x1F;
      if (inst.op == OPCODE_COP0 && cop_op < 32 && s_cop0_table[cop_op]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", s_cop0_table[cop_op]);
        format_instruction(dest, dest_size, &inst, pc, buf);
      } else {
        // Handle common coprocessor instructions
        uint32_t common_op = (bits >> 21) & 0x1F;
        const char* cop_name = s_cop_names[inst.op - OPCODE_COP0];
        char buf[64];
        if (common_op == 0) {
          snprintf(buf, sizeof(buf), "mfc%s $rt, $coprd", cop_name + 3);
        } else if (common_op == 2) {
          snprintf(buf, sizeof(buf), "cfc%s $rt, $coprdc", cop_name + 3);
        } else if (common_op == 4) {
          snprintf(buf, sizeof(buf), "mtc%s $rt, $coprd", cop_name + 3);
        } else if (common_op == 6) {
          snprintf(buf, sizeof(buf), "ctc%s $rt, $coprdc", cop_name + 3);
        } else {
          snprintf(buf, sizeof(buf), "UNKNOWN_%s_%u", cop_name, common_op);
        }
        format_instruction(dest, dest_size, &inst, pc, buf);
      }
      break;
    }

    case OPCODE_B: {
      uint32_t rt = inst.i.rt;
      const char* format;
      if (rt == 0) {
        format = "bltz $rs, $rel";
      } else if (rt == 1) {
        format = "bgez $rs, $rel";
      } else if (rt == 16) {
        format = "bltzal $rs, $rel";
      } else if (rt == 17) {
        format = "bgezal $rs, $rel";
      } else {
        format = "UNKNOWN_B";
      }
      format_instruction(dest, dest_size, &inst, pc, format);
      break;
    }

    default:
      if (inst.op < 64 && s_base_table[inst.op]) {
        format_instruction(dest, dest_size, &inst, pc, s_base_table[inst.op]);
      } else {
        snprintf(dest, dest_size, "UNKNOWN");
      }
      break;
  }
}

void cpu_disassemble_instruction_comment(char* dest, size_t dest_size, uint32_t pc, uint32_t bits) {
  char disasm[128];
  cpu_disassemble_instruction(disasm, sizeof(disasm), pc, bits);

  // Add basic comment for load/store operations
  Instruction inst = {.bits = bits};
  if (inst.op >= OPCODE_LB && inst.op <= OPCODE_LWR) {
    // Load instruction
    uint32_t addr = (uint32_t)((int32_t)inst.i.imm + (inst.i.rs ? 0 : 0)); // Simplified
    snprintf(dest, dest_size, "%-20s ; load from [r%d%+d]", disasm, inst.i.rs, (int16_t)inst.i.imm);
  } else if (inst.op >= OPCODE_SB && inst.op <= OPCODE_SWR) {
    // Store instruction
    uint32_t addr = (uint32_t)((int32_t)inst.i.imm + (inst.i.rs ? 0 : 0)); // Simplified
    snprintf(dest, dest_size, "%-20s ; store to [r%d%+d]", disasm, inst.i.rs, (int16_t)inst.i.imm);
  } else if (inst.op == OPCODE_J || inst.op == OPCODE_JAL) {
    // Jump instruction
    uint32_t target = (pc & 0xF0000000) | (inst.j.target << 2);
    snprintf(dest, dest_size, "%-20s ; jump to 0x%08X", disasm, target);
  } else if (inst.op >= OPCODE_BEQ && inst.op <= OPCODE_BGTZ) {
    // Branch instruction
    int32_t offset = (int16_t)inst.i.imm << 2;
    uint32_t target = pc + 4 + offset;
    snprintf(dest, dest_size, "%-20s ; branch to 0x%08X", disasm, target);
  } else {
    // Default - just copy disassembly
    strncpy(dest, disasm, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
}

const char* cpu_get_gte_register_name(uint32_t index) {
  static const char* gte_names[64] = {
    "vxy0", "vz0", "vxy1", "vz1", "vxy2", "vz2", "rgb", "otz",
    "ir0", "ir1", "ir2", "ir3", "sxy0", "sxy1", "sxy2", "sxyp",
    "sz0", "sz1", "sz2", "sz3", "rgb0", "rgb1", "rgb2", "res1",
    "mac0", "mac1", "mac2", "mac3", "irgb", "orgb", "lzcs", "lzcr",
    "rt11", "rt12", "rt13", "rt21", "rt22", "rt23", "rt31", "rt32",
    "rt33", "trx", "try", "trz", "l11", "l12", "l13", "l21",
    "l22", "l23", "l31", "l32", "l33", "rbk", "gbk", "bbk",
    "lr1", "lr2", "lr3", "lg1", "lg2", "lg3", "lb1", "lb2", "lb3"
  };

  if (index < 64) {
    return gte_names[index];
  }
  return NULL;
}

// ============================================================
// Public API Implementation (compatibility layer)
// ============================================================

const char* cpu_disassemble(uint32_t instruction, uint32_t pc) {
  static char buffer[256];
  cpu_disassemble_instruction(buffer, sizeof(buffer), pc, instruction);
  return buffer;
}