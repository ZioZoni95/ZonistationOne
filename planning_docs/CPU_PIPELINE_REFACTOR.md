# CPU Pipeline and Hazards Refactor (January 2026)

This document details the refactoring of the CPU core to align its pipeline, branching, and hazard handling with the behavior of mature emulators like DuckStation, based on hardware tests and PSX-Spex documentation.

## Summary of Changes

- **DuckStation-aligned CPU Core:** The main execution loop (`cpu_run_next_instruction`), branch logic, and load delay slot mechanism were updated to more accurately reflect the MIPS R3000A's pipeline behavior.
- **Load Delay Slot Handling:** All load instructions (`LB`, `LH`, `LW`, `LBU`, `LHU`, `LWL`, `LWR`, `MFC0`, `MFC2`, `CFC2`) now use a `cpu_set_reg_delayed` function. This ensures that the result of a load is not available to the immediately following instruction, correctly emulating the load delay slot.
- **Pipeline Hazard Management:**
  - **Load-Use Hazards:** The new load delay mechanism prevents load-use hazards.
  - **MULDIV Hazards:** All `MULT`/`DIV` and `MFHI`/`MFLO`/`MTHI`/`MTLO` instructions now include stall logic to wait for any in-progress multiplication or division to complete, preventing read-after-write and write-after-write hazards on the `HI`/`LO` registers.
- **Exception Handling in Delay Slots:** Exception handling was improved to correctly save state when an exception occurs in a branch delay slot. The `EPC` is now set to the address of the branch instruction, and the `TAR` register is updated with the branch target address, matching hardware behavior.
- **COP0/COP2 Instruction Updates:** `MTC0`, `MFC0`, `RFE`, and all `COP2` (GTE) instructions were updated to correctly handle their respective registers and side effects, including interrupt dispatch checks after modifying the `SR` or `CAUSE` registers.

## Key Files Modified

- `src/cpu/cpu_core.c`: Updated main execution loop and pipeline logic.
- `src/cpu/cpu_instructions.c`: Refactored all load, MULDIV, and COP0/2 instructions.
- `src/cpu/cpu_exceptions.c`: Improved exception context saving.
- `include/cpu/cpu_core.h`: Added `cpu_set_reg_delayed`.

This refactoring significantly improves the emulator's accuracy regarding CPU pipeline behavior, which is critical for running games that are sensitive to instruction timing and hazards.
