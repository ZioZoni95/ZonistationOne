# ZonistationOne — Structural Gap Analysis & Refactor Roadmap

**Date**: 2026-07-13
**Compared against**: `duckstation_ref/` (DuckStation, C++) and `pcsx-redux/` (PCSX-Redux, C-style) — both local clones in this repo.
**Scope**: structural/architectural comparison, component by component. Not a line-by-line code review. Focus is on gaps that matter for a planned 1:1 structural refactor: dead/duplicate code, missing abstractions, incomplete features, and organizational issues — including minor ones.
**Status**: Phase 0 (cleanup) complete as of 2026-07-13 — see Changelog at bottom. Findings below are left as originally written (historical record); items resolved by Phase 0 are marked inline with ✅.

---

## Methodology

Three research passes fed this document:
1. Full inventory of this project's own `src/`+`include/` — every component, LOC, every `TODO`/`FIXME`/stub marker found via grep, dead-code detection via Makefile cross-reference.
2. DuckStation `src/core/` architecture per component — how state is structured, module boundaries, key design choices (notably its `timing_event.cpp` event scheduler and `Controller` abstraction).
3. PCSX-Redux `src/core/` architecture per component — same set, with attention to patterns most portable to plain C99 (PCSX-Redux's own style is closer to C than DuckStation's).

Where the two references disagree, this doc recommends the pattern most portable to C99 — usually PCSX-Redux's, adapted (e.g. function-pointer struct instead of virtual class) — or a synthesized minimal version.

## Priority Framework

- **Critical** — dead/duplicate code causing confusion or real bugs, or missing functionality actively blocking correctness (e.g. two competing IRQ controllers, two competing event schedulers).
- **High** — structural gap likely to cause real bugs or block compatibility (e.g. no PAL support, a BIOS syscall handler that's a no-op stub).
- **Medium** — incomplete-but-low-impact features (e.g. one DMA channel stubbed, an unreachable-but-otherwise-complete feature).
- **Low** — cosmetic/organizational (misplaced file, stale comment, duplicated `#define` block).

---

## 1. CPU / Interpreter Core

**Files**: `src/cpu/cpu_instructions.c` (999L), `cpu_bios.c` (332L), `cpu_execution.c` (260L), `cpu_disasm.c` (176L), `cpu_decode.c` (94L), `cpu_exceptions.c` (87L), `cpu_icache.c` (83L), `cpu_init.c` (71L), `cpu_registers.c` (44L), `include/cpu.h` (336L).

**Current state**: Full MIPS-I R3000A instruction set via function-pointer dispatch tables (`s_op_table[64]`, `s_special_table[64]`), delay-slot load/branch handling, dual-buffer GTE load-delay, cache-isolation (`SR.IsC`) handling for stores, DuckStation-style `downcount`/event-driven cycle accounting, MULT/DIV latency stalls, and an 8192-entry execution trace ring buffer with crash-dump capability. BIOS syscalls are intentionally LLE with an HLE side-channel for TTY/debug capture only (real BIOS code runs; A0/B0/C0 handlers just snoop `$t1`/args).

**Reference pattern**:
- *DuckStation*: flat `State` struct holds everything (regs/cop0/downcount/icache/scratchpad/GTE inline). Real two-PC pipeline model (`pc`/`npc` + current/next instruction + branch-delay flags) rather than a peek-ahead hack. Load delay uses a dummy extra register slot (`r[33]`) plus current/next load-delay pairs, correctly handling "load into same register twice in a row." Downcount is checked at loop boundaries, not per instruction. Execution mode (interpreter vs recompiler) is a runtime strategy switch with a templated debug flag for zero hot-path overhead when disabled.
- *PCSX-Redux*: flat `psxRegisters` with a 64-bit cycle counter + interrupt bitmask + parallel `intTargets[32]` deadline array + `lowestTarget` cache. Multi-level function-pointer dispatch tables, no giant switch. Delay slots via `m_nextIsDelaySlot`/`m_inDelaySlot` flags; load delay via a double-buffered slot ping-ponged each instruction. Flat per-instruction cycle cost (`BIAS`), simpler/less accurate than this project's per-op model. **Key pattern**: interrupts are checked lazily in `branchTest()`, called once per branch — not per instruction.

**Gaps**:
1. ✅ **RESOLVED (Phase 0)** — Dead/vestigial code in `op_jr`/`op_jalr` (`cpu_instructions.c` ~lines 283-289, 417-434): empty `if(){}` blocks and a large commented-out duplicate HLE logic block left over from an HLE→LLE migration. — was **Low**
2. `cpu_icache.c:23` — `SR.IsC` handled for stores, but `SR.SwC` (cache-swap) is not — TODO comment present. — **Medium**
3. COP0 breakpoint registers (BPC/BDA/BDAM/BPCM) unimplemented (reads 0, writes ignored) — matches real hardware rarely being exercised this way; expected/acceptable given the software debugger covers this need instead. — **Low**

**Recommended action**: ~~Delete the dead code blocks in `op_jr`/`op_jalr`.~~ Done. Add `SR.SwC` handling for completeness (still open). No structural rework needed — dispatch-table and downcount design already match both references' patterns well.

---

## 2. Bus / Interconnect / Memory Dispatch

**Files**: `src/core/bus.c` (811L), `src/core/interconnect.c` (162L), `include/interconnect.h` (320L).

**Current state**: `bus.c` uses a 256-entry function-pointer dispatch table (`g_hw_read[256]`/`g_hw_write[256]`, indexed by `(phys>>4)&0xFF`) for the `0x1F801000-0x1F801FFF` hardware register window, built once at init.

**Reference pattern**:
- *DuckStation*: page-granularity (4KB) function-pointer LUT indexed by `[access size][read/write][page]`, not a switch — plus a **separate parallel LUT for cache-isolated mode**, swapped wholesale rather than branched per-handler. HW region dispatches a second level to per-peripheral handler classes.
- *PCSX-Redux*: **512-entry LUT indexed by `address >> 16`** (64KB granularity) gives O(1) direct pointer access for RAM/BIOS/EXP1 mirrors across KUSEG/KSEG0/KSEG1 with zero branching — flagged by that research pass as the single highest-value portable pattern in their whole codebase for a C99 bus. HW register *storage* is centralized in `Memory`; HW register *behavior* is a thin switch-based trampoline in a separate module — a clean storage/behavior split.

**Gaps**:
1. `interconnect.c` contains a **private, parallel event system** — an 8-slot `CdromEvent` array — entirely separate from the real `event_scheduler.c`. Two independent scheduling mechanisms coexist. (Full detail under §6 Event Scheduler — this is the same root issue, cross-referenced here because the array physically lives in this file.) — **Critical**
2. Hard-coded RAM address "WATCHPOINT" ranges baked directly into `interconnect_load32/16/8` and the matching stores — game-specific debug instrumentation in the hot memory-access path, while a real `Debugger` watchpoint mechanism (`debugger_check_read/write_watchpoint`) already exists and goes unused by `bus.c`. — **Medium**
3. ✅ **RESOLVED (Phase 0)** — `interconnect_check_bios_boot()` — dead one-line no-op stub (`(void)inter;`). — was **Low**
4. ✅ **RESOLVED (Phase 0)** — `interconnect.h` defines `TIMERS_START`/`TIMERS_SIZE`/`TIMERS_END` **twice** (copy-paste duplication within the same header, ~lines 28-30 and 67-69). — was **Low**
5. Top-level KUSEG/KSEG0/KSEG1 address routing structure not confirmed as LUT vs switch/if-chain during this pass — worth a direct read during the actual refactor to compare against PCSX-Redux's 64KB-granularity LUT pattern. — **flag for verification, not a confirmed gap**

**Recommended action**: Remove hard-coded WATCHPOINT ranges from the hot path, route through the existing `Debugger` watchpoint mechanism instead (still open, Phase 3). ~~Fix the duplicated `TIMERS_*` defines.~~ ~~Delete the dead `interconnect_check_bios_boot()`.~~ Both done. Defer top-level LUT restructuring to the DMA/Bus refactor phase (Phase 3) pending direct-read verification.

---

## 3. Interrupt Controller

✅ **RESOLVED (Phase 0)** — `src/interrupt_controller.c` + `include/interrupt_controller.h` deleted; `interconnect_debug_check_irq_status()` deleted. Section kept below as historical record of the finding.

**Files (live)**: `src/core/bus_irq.c` (51L) + IRQ fields embedded directly in the `Interconnect` struct.
**Files (dead, now deleted)**: ~~`src/interrupt_controller.c` (49L) + `include/interrupt_controller.h` (37L)~~.

**Original finding — confirmed dead code**: `src/interrupt_controller.c`/`.h` implement a self-contained `InterruptController` struct with a full `init/read_status/read_mask/write_status/write_mask/set_line/has_pending` API. It is **not referenced in the Makefile** (absent from every source list, including the test build) and **not included anywhere** except by itself. The actually-used implementation is entirely different: `Interconnect` holds `irq_status`/`irq_mask`/`irq_line_state` as plain fields, operated on by free functions in `bus_irq.c` (`interconnect_set_irq_line`, `interconnect_request_irq`, `interconnect_clear_irq`, `interconnect_trigger_cdrom_irq`). I_STAT/I_MASK register I/O is handled directly in `bus.c`. The two implementations even carried **duplicate, independently-maintained IRQ-number enumerations** (`IRQNumber` enum in the dead header vs `#define IRQ_VBLANK 0..IRQ_PIO 10` macros in the live one) that could have drifted if either was edited in isolation.

Also dead (now deleted): `bus_irq.c:50` — `interconnect_debug_check_irq_status()` was an always-empty function, declared but never called.

**Reference pattern** (DuckStation `interrupt_controller.cpp`, 119L — deliberately minimal): I_STAT/I_MASK plus one extra hidden register, `s_interrupt_line_state`, tracked separately from the latched status register — this is what implements true edge-triggering: the STAT bit is set only on a 0→1 transition of the line (not on line-drop), matching real hardware where a device asserting-then-deasserting between polls still latches. `UpdateCPUInterruptRequest()` is a single push notification computed after every STAT/MASK read-modify-write, not polled per cycle. **Takeaway**: DuckStation's reference is *not* structurally richer than a well-implemented edge-triggered I_STAT/I_MASK — the live implementation (`bus_irq.c` + `Interconnect` fields tracking `irq_line_state` separately) already matches this shape. The gap here was pure dead code, not missing structure.

**Priority**: was **Critical** — not because the live implementation was wrong, but because the dead duplicate was a navigation hazard during a refactor (easy to edit the wrong file, or assume both are live).

**Recommended action**: ~~Delete `src/interrupt_controller.c` and `include/interrupt_controller.h` entirely. Delete the dead `interconnect_debug_check_irq_status()`.~~ Done. Still open: explicitly verify `interconnect_set_irq_line`'s edge-detect semantics against DuckStation's "latch only on 0→1 transition, ignore line-drop" rule as a correctness check (read of `bus_irq.c` during Phase 0 shows this already matches — treat as confirmed-by-inspection, not yet regression-tested).

---

## 4. DMA

**Files**: `src/core/dma.c` (217L), `include/dma.h`.

**Current state**: 7-channel `DmaChannel channels[7]` array, complete register I/O (MADR/BCR/CHCR, DPCR, DICR) for all channels. Actual transfer wiring (in `interconnect_perform_dma`, `bus.c`):

| Ch | Device | Status |
|---|---|---|
| 0 | MDEC in | ✅ wired |
| 1 | MDEC out | ✅ wired |
| 2 | GPU | ✅ wired (sliced LL + REQUEST/MANUAL) |
| 3 | CDROM | ✅ wired (to-RAM only — correct, matches real HW) |
| 4 | SPU | ✅ wired both directions |
| **5** | **PIO** | ❌ falls to `default:` — silently no-ops |
| 6 | OTC | ✅ wired (special-cased in `bus.c`) |

`chcr_unknown_rw` bits 29-30 explicitly not implemented (commented out, undocumented/unused on real hardware). GPU DMA slicing logic lives in `bus.c` rather than `dma.c` — mild separation-of-concerns issue, not a functional gap.

**Reference pattern**:
- *DuckStation*: channels as `std::array<ChannelState>` (shared POD struct); behavior is compile-time specialized per channel via templates, with a static array of pre-instantiated function pointers giving O(1) dispatch (not a per-instance callback member). Full bitfield unions for DPCR/DICR with `UpdateMasterFlag()`/`ShouldSetIRQFlag()` helpers implementing the correct OR-reduction logic. Linked-list/request/manual modes handled inside one templated `TransferChannel<channel>()` via a `SyncMode` branch.
- *PCSX-Redux*: no unified per-channel struct — channel state lives as raw bytes in the shared HW register block, accessed via templated getters. Behavior is scattered by ownership: channels 0-3 are one-line forwards to the owning module's `dma()` method; channels 4 (SPU) and 6 (OTC) are standalone free functions because they lack a natural owning object. **Notable pattern**: a *generic* linked-list chain-walker (used by GPU/OTC modes) includes a loop-guard counter and a small address history (`usedAddr[3]`) to detect and break infinite loops in malformed linked lists — shared by all channels rather than duplicated.

**Gaps**:
1. DMA channel 5 (PIO/Expansion) has zero data path — register access works, no transfer logic. Real hardware rarely uses this, but it currently fails silently rather than being documented as an intentional gap. — **Medium**
2. Not confirmed whether the GPU/OTC linked-list walker has an infinite-loop guard matching PCSX-Redux's defensive pattern — worth a direct check; a malformed linked list could hang the emulator without one. — **flag for verification**
3. `chcr_unknown_rw` bits 29-30 unimplemented. — **Low** (undocumented on real hardware)

**Recommended action**: Either implement a minimal PIO DMA passthrough or explicitly log+document channel 5 as "not implemented, no known title requires it" instead of silent fallthrough. Verify (and if missing, add) a loop-guard on the GPU/OTC linked-list walker.

---

## 5. Timers

**Files**: `src/core/timers.c` (466L), `include/timers.h`.

**Current state**: Full 3-timer model with sync modes, IRQ pulse/toggle behavior, per-timer clock source selection (sysclock/dotclock/hblank/div8), fractional-cycle accumulation, event-scheduler integration (`timers_schedule_next_event`). Includes a PCSX-ReARMed-attributed BIOS-boot compatibility hack (`timer_force_bios_boot_config`) baked into core timer logic.

**Reference pattern**:
- *DuckStation*: counters as `std::array<CounterState>`; a **single shared TimingEvent** drives all 3 for sysclk-sourced ticking, with timer 2 accumulating a `/8` carry. Reschedule distance recomputed via `GetTicksUntilNextInterrupt()` (min over all counters) after any state change — push-model. External clock sources (dotclock, hblank) are **not owned by the timer module** — GPU pushes ticks/gate state directly into Timers (cross-module push, not polling). `InvokeEarly()` forces a resync before any register write that affects scheduling — a "flush pending state before mutating scheduled event" idiom also seen in DuckStation's DMA/CDROM/SPU.
- *PCSX-Redux*: single `Rcnt[4]` array (index 3 = internal hsync pseudo-counter for CRTC timing, not exposed to the guest). **Lazy virtual-clock model**: counters are not incremented per cycle, they're computed on-demand as `(cycle - cycleStart) / rate`. `recalculateRate()` handles GPU-resolution-dependent dot-clock rate. SPU audio-frame pacing and SIO1 polling are piggybacked into the same per-scanline update — Counters ends up as a general event pump, not just a timer peripheral.

**Gaps**:
1. `timers_handle_setrcnt()` (BIOS `SetRCnt` syscall handler) is a **pure stub** — body is just a TODO comment, does nothing. `SetRCnt` is a real, occasionally-used BIOS syscall; silently no-op-ing it can desync games that call it expecting side effects. — **High**
2. `timers_calculate_frame_cycles()` is hard-coded to NTSC 60Hz — no PAL support threaded through, despite `VMode`/`Pal` already existing in GPU state (TODO comment present at the call site). Blocks correct frame timing for any PAL-region BIOS/game. — **High**
3. ✅ **RESOLVED (Phase 0)** — Six `bios_init_timer`/`bios_get_timer`/`bios_enable_timer_irq`/`bios_disable_timer_irq`/`bios_restart_timer`/`bios_ChangeClearRCnt` functions were explicit dead placeholder stubs, vestigial from an earlier HLE-era API, explicitly commented as "not used by BIOS itself." — was **Low**

**Recommended action**: Implement `SetRCnt` per PSX-SPX spec (reconfigure a timer's mode/target from BIOS args) — still open, Phase 3. Thread the GPU's `VMode`/`Pal` flag into `timers_calculate_frame_cycles()` — still open, Phase 3. ~~Delete the dead `bios_*` stub functions.~~ Done.

---

## 6. Event Scheduler

**Files**: `src/core/event_scheduler.c` (193L), `include/event_scheduler.h`.

**Current state**: 13 event types (`EVQ_VBLANK`, `TIMER0-2`, `DMA_GPU`, `DMA_CDROM`, `DMA_SPU`, `DMA_OTC`, `SIO`, `CDROM`, `GPU`, `MDEC`, `SPU`), bitmask-pending + target-cycle-array design, dispatched from the CPU's `downcount` mechanism.

**This is the single most important cross-cutting gap in the codebase.** `interconnect.c` maintains a **second, entirely private event system** — a hand-rolled 8-slot `CdromEvent` array (`interconnect_schedule_event`/`interconnect_check_cdrom_events`) — completely separate from this "real" scheduler. `evq_handle_dma_cdrom` and `evq_handle_cdrom` in `event_scheduler.c` are both explicit no-op passthroughs, with comments noting the real logic lives in the CDROM subsystem's own private array. Meanwhile `EVQ_DMA_SPU`, `EVQ_DMA_OTC`, and `EVQ_GPU` (non-DMA) map to `NULL` handlers — declared/schedulable but silent no-ops if ever fired (SPU is intentionally handled by its own dedicated thread instead — fine — but the DMA_SPU/DMA_OTC/GPU slots are just dead enum values). ~~Stale comments ("Event handler stubs for all timer events" / "Example Event Handlers (Stubs)") are misleading now that most of the table is production code.~~ ✅ **RESOLVED (Phase 0)** — comments updated to reflect production status.

**Reference pattern** (DuckStation `timing_event.cpp` — the key architectural pattern DuckStation is built around):
- **Sorted intrusive doubly-linked list**, not a min-heap and not a linear scan-and-pick-min. Reschedule uses a local bubble from the event's current position — O(1) amortized for periodic events (VBlank, timers) that stay near their old position.
- Two time domains reconciled explicitly: `GlobalTicks` (absolute) for the queue vs `TickCount` (relative) for the CPU's `pending_ticks`/`downcount`. The "true now" used for scheduling includes ticks the CPU has executed but not yet committed — lets the interpreter batch instructions before paying the cost of walking the event list.
- Downcount recalculation is **push-driven**: recomputed whenever the head of the list changes, from every call site that could change it (add/remove/sort/delay/schedule/invoke-early) — not recomputed every CPU cycle.
- **Catch-up/"late" execution is built into normal dispatch**: a loop over the head event runs (and re-sorts it back in) as long as global time has passed its scheduled time, computing elapsed ticks correctly for a late-firing event. Critically, the **next scheduled time is computed from `old_next_run_time + interval`, not `now + interval`** — this is what avoids long-term drift if an event ever fires late (e.g. under a slow catch-up frame). `InvokeEarly(force)` lets other subsystems force an event to run early without waiting for its natural downcount — the same idiom noted under Timers/DMA/SPU.

**Gaps**:
1. Dual event schedulers (CDROM private array vs real EVQ mechanism) — the CDROM subsystem's timing is independently calibrated and currently working, so unifying this is a real "don't break it" migration, not a trivial delete. — **Critical**
2. Not confirmed whether this project's rearm-on-late-fire logic uses `old_next_run_time + interval` or `now + interval` — the latter causes silent long-term timer/CRTC drift if an event is ever dispatched late. Cheap to check, high-value if wrong. — **High**
3. Dead `EVQ_DMA_SPU`/`EVQ_DMA_OTC`/`EVQ_GPU` enum slots with `NULL` handlers. — **Low**
4. ✅ **RESOLVED (Phase 0)** — Stale misleading comments. — was **Low**

**Recommended action** (roadmap Phase 1): Migrate CDROM's private `CdromEvent` array onto the real `EVQ_CDROM`/`EVQ_DMA_CDROM` mechanism, using DuckStation's 4-event split (command / second-response / async-interrupt / drive) as the target shape, since CDROM's internal event separation already implies this shape (see §10). Do this with explicit regression testing against known-good boot + disc-read behavior — CDROM timing is calibrated and currently correct. Separately, audit the rearm math for drift-correctness. Remove or document the dead `EVQ_*` enum slots. Update stale comments.

---

## 7. SIO / Controllers / Memory Cards

**Files**: `src/core/sio.c` (817L), `src/core/controller.c` (112L), `include/sio.h`, `include/controller.h`.

**Current state**: DuckStation-ported byte-stepped SIO state machine (`IDLE`/`TRANSMITTING`/`WAITING_FOR_ACK`), digital-pad protocol (ID 0x41/0x5A, 4-step transfer), full memory-card SPI protocol with `.mcd` file persistence, event-scheduled byte transfers via `EVQ_SIO`.

**Gaps** — this section has the clearest missing-abstraction finding in the whole audit:
1. **Three parallel copies of "current button state" exist simultaneously**: `Controller.button_state` (raw SDL keyboard poll result), `SioInternal.button_state` (private module-level singleton — the one actually used by the transfer state machine), and `Sio.button_state` (a field on the *public* struct that's never actually read — real state lives in the private singleton). `main.c` wires these together each frame, functionally, but the duplication is the structural root cause of gap #2 below. — **Critical**
2. Only memory card slot 1 is functional. `card_slot2`/`mc2`/`card_slot2_present` exist in the struct and are initialized, but `sio_do_transfer()`'s device-select logic only ever checks `card_slot1_present`, and `sio_memcard_transfer()` hard-codes `s->mc1`. — **High**
3. `ActiveDevice` enum includes `ACTIVE_DEVICE_MULTITAP` but it's never assigned or handled anywhere. — **High**
4. Digital pad only — no analog stick/DualShock, no rumble, no lightgun/mouse. `sio.h` literally comments "Controller state (basic stub for now)." — **Low** (feature gap, not structural; most current testing doesn't need it)
5. The public `Sio` struct duplicates several fields (`tx_data`, `rx_data`, `stat`, `mode`, `ctrl`, `baud`, `transfer_step`, `rx_buffer`/`tx_buffer`) that are dead shadows of the real state in the private `SioInternal` singleton. — **Low**

**Reference pattern**:
- *DuckStation*: a real generic abstract `Controller` base class — virtual `GetType`/`Reset`/`DoState`/`ResetTransferState`/`Transfer(u8 in, u8* out)`/`GetButtonStateBits`/`GetAnalogInputBytes`, with a `Controller::Create(type, index)` factory. `DigitalController`/`AnalogController`/`AnalogJoystick`/`DDGoController` all derive from it — digital pad is just one concrete subclass, not hardcoded. Memory cards are a second polymorphic device family (`MemoryCard*` array parallel to `Controller*`) sharing the same port/transfer machinery. Note: DuckStation's `sio.cpp` is a *different*, near-stub physical unit (the SIO1 link-cable UART) — not to be confused with `pad.cpp` (the real controller/memcard port, SIO0). This project's `sio.c` is the SIO0/pad equivalent — worth keeping the naming distinction in mind if ever cross-referencing DuckStation source directly.
- *PCSX-Redux*: `SIO` owns the byte-serial protocol directly; `MemoryCard` is a nested owned object with a back-pointer to `SIO`, protocol delegated there. `Pads` is a **separate** abstract plugin interface (`startPoll`/`poll`/`getCfg`/`setCfg`) that `SIO` calls into when the selected device is a pad — controller-type-specific behavior lives behind this interface, decoupled from the byte-shifting protocol itself.

**Recommended action** (roadmap Phase 2): Introduce a minimal C "Controller" abstraction — a struct of function pointers (`reset`/`transfer_byte`/`get_button_state`) rather than a class hierarchy, modeled on PCSX-Redux's simpler `Pads` interface. Have `sio.c` own exactly one input-state source (delete `Controller.button_state` and the dead `Sio.button_state`, keep only the source that feeds the transfer state machine). Wire `card_slot2` through the same code path as slot 1, parametrized by index instead of hard-coded `s->mc1`. This one refactor resolves three separate gap items (#1, #2, and lays groundwork for #3) at once.

---

## 8. GPU / Renderer / VRAM

**Files**: `src/gpu/gpu_commands.c` (1480L), `renderer.c` (1499L), `gpu.c` (469L), `gpu_helpers.c` (131L), `vram.c` (101L). (`debugger.c`, 208L, moved to `src/core/` in Phase 0 — see below.)

**Current state**: Essentially complete GP0/GP1 command coverage via a 256-entry dispatch table — mono/shaded/textured tri & quad (opaque/semi-transparent, raw/blend), all rectangle size variants, mono/shaded lines and polylines with terminator-detection, VRAM load/store with correct mask-bit handling, VRAM-to-VRAM copy, fill rectangle. Full CRTC scanline/vblank/odd-even tracking, bit-for-bit GPUSTAT reconstruction, `GetGPUInfo` subfunctions. **No TODO/stub markers found anywhere in this subsystem** — alongside GTE, the most mature/complete part of the codebase.

**Structural gap**: rendering is **exclusively hardware OpenGL** — `renderer.c`/`.h` types are OpenGL-native throughout (VAO/VBO/shader state embedded directly in the `Renderer` struct, itself embedded in `Gpu`). No renderer-abstraction interface exists, so there's no software fallback and no separation between "GPU command logic" and "GPU presentation." The GPU runs on a dedicated `SDL_Thread` with mutex/condvar handoff (double-buffered batch submission) — a legitimate threading design, but the CPU-side and render-backend state are tightly coupled.

Also: ✅ **RESOLVED (Phase 0)** — `debugger.c` (a generic CPU/memory breakpoint+watchpoint debugger, used from `cpu_execution.c`) used to live under `src/gpu/` for no GPU-specific reason; moved to `src/core/debugger.c`, Makefile updated (`EMU_GPU_SRCS` → `EMU_CORE_SRCS`).

**Reference pattern**:
- *DuckStation*: three-layer split — CPU-thread-side GP0/GP1 decode + CRTC timing (backend-agnostic, shared) → abstract `GPUBackend` with a producer/consumer command queue to a dedicated video thread (decouples CPU-thread timing from actual render work) → concrete `GPU_HW`/`GPU_SW`/null backends. GP0 dispatch itself is a 256-entry function-pointer table, same pattern already used here.
- *PCSX-Redux*: also an abstract-base `GPU` with pure-virtual backend hooks (same plugin pattern as its SPU/Pads/CDRom). Explicitly flagged by that research pass as the **least representative "plain-C" module** in an otherwise C-style codebase — the portable takeaway is just "3-bit type + 5-bit command → jump table" (already present here), not the templated-struct-with-embedded-FIFO primitive objects.

**Priority**: **Medium** — works correctly today; hardware-only is a real limitation for debugging/headless/portability but doesn't block current correctness work, and this is otherwise the most polished subsystem in the project. `debugger.c` misplacement: was **Low**, now resolved.

**Recommended action**: ~~Move `debugger.c` out of `src/gpu/` now (Phase 0, free win, zero risk).~~ Done. Defer full backend abstraction (software rasterizer) to Phase 5 as an optional, low-urgency item — if pursued, model it as a C function-pointer vtable struct (`draw_polygon`/`draw_line`/`read_vram`/`fill_vram`/`update_display`) rather than a virtual class.

---

## 9. GTE

**Files**: `src/gte/gte.c` (156L), `gte_ops.c` (440L).

**Current state**: All documented GTE opcodes implemented (`RTPS, NCLIP, OP, DPCS, INTPL, MVMVA, NCDS, CDP, NCDT, NCCS, CC, NCS, NCCT, NCT, SQR, DCPL, DPCT, AVSZ3, AVSZ4, RTPT, GPF, GPL`), correct per-opcode cycle counts (independently re-verified via prior bug-fix commits in this repo's history), UNR reciprocal table, sign-extension/saturation helpers, full 32+32 register file with correct special-case behavior (IRGB/ORGB packing, LZCS/LZCR, SXY FIFO shift). **Zero TODO/stub markers found** — second most complete subsystem after GPU.

**Reference pattern**: DuckStation dispatches via a plain switch with cycle cost hardcoded inline per case (matches documented timing); GTE registers live inside `CPU::State` for codegen reasons. PCSX-Redux splits register-transfer instructions (`gte-transfer.cc`) from math instructions (`gte-instructions.cc`) — a split this project's `gte.c`/`gte_ops.c` division already mirrors. Notably, PCSX-Redux's interpreter does **not** model variable per-instruction GTE cycle cost (flat cost) — this project, having already fixed per-op cycle accuracy, is *more* accurate than that reference here.

**Gap**: essentially none, structurally. This section is close to a "no gap" finding.

**Priority**: Low / informational.

**Recommended action**: No refactor needed. Useful as a baseline reference for coding standard when refactoring neighboring components (CPU dispatch, SIO).

---

## 10. CDROM

**Files**: `src/cdrom/cdrom_commands.c` (779L), `cdrom_disc.c` (358L), `cdrom.c` (403L), `cdrom_audio.c` (301L).

**Current state**: Near-full command set (`SYNC, GETSTAT, SETLOC, PLAY, FORWARD, BACKWARD, READN, MOTORON, STOP, PAUSE, INIT, MUTE, DEMUTE, SETFILTER, SETMODE, GETPARAM, GETLOCL, GETLOCP, READT, GETTN, GETTD, SEEKL, SEEKP, SETCLOCK, GETCLOCK, TEST, GETID, READS, RESET, GETQ, READTOC, VIDEOCD`), async/threaded disc reader (`pthread_t`), event-driven command/drive/second-response scheduling with delay constants calibrated to match PCSX-Redux's timing model, BIN/CUE multi-track loading, SubQ generation, full CDDA+XA-ADPCM decode (zigzag interpolation resampler at both 37800Hz/18900Hz, filter tables, IIR predictor) feeding a dedicated audio FIFO mixed with SPU output, spec-correct volume matrix. **Zero TODO/stub markers found.**

**Reference pattern**: DuckStation models CDROM with **four separate `TimingEvent`s** (command / second-response / async-interrupt / drive-mechanism), an async disc-reader thread decoupling host I/O latency from emulated timing (this project already has the pthread equivalent), three fixed-capacity FIFOs (param/response/async-response, kept separate because async completions must queue independently of the current sync response), and double-buffered sector storage to avoid tearing. PCSX-Redux uses the same delayed-response-via-scheduled-interrupt pattern this project already implements, with multiple parallel scheduled event types rather than one FSM — matching DuckStation's multi-event split.

**Gap**: CDROM is already structurally well-aligned with both references' multi-event, async-reader, delayed-response design. The **only** real gap is that its scheduling currently rides on the private ad-hoc array in `interconnect.c` rather than the shared `event_scheduler.c` — already captured as the Critical finding in §6.

**Priority**: Low as a standalone item (the real issue is tracked centrally under Event Scheduler).

**Recommended action**: No CDROM-specific work beyond the Phase 1 event-scheduler unification. When performing that migration, use DuckStation's 4-event split as the target shape for the CDROM-family EVQ slots.

---

## 11. SPU

**Files**: `src/spu/spu_mixing.c` (454L), `spu.c` (374L), `spu_voice.c` (319L), `spu_adsr.c` (163L), `spu_dma.c` (125L), `spu_irq.c` (50L).

**Current state**: Full 32-register reverb implementation (IIR filters, 4 accumulator taps, feedback comb, correct SPU-RAM delay-line addressing), LFSR-style noise generator with per-voice noise-mode bit, pitch modulation (voice N modulated by voice N-1's output) — **none of these are stubbed**. Full 24-voice ADPCM decode with Gaussian interpolation, complete ADSR state machine (Attack/Decay/Sustain/Release/Stopped, linear/exponential, rate tables), SPU DMA both directions, IRQ9 address-watch tied into the bus's edge-triggered I_STAT logic. Dedicated `SDL_Thread` with a lock-free SPSC ring buffer feeding SDL audio. **Zero TODO/stub markers found** — explicit PCSX-Redux attribution throughout comments; the most directly "ported-from-reference" component in the codebase.

**Reference pattern**: DuckStation also fully implements reverb/noise, with dedicated downsample/upsample ring buffers reconciling 22050Hz reverb processing against 44100Hz output, and forces `GeneratePendingSamples()` before any register write mutates state ("flush before mutate," the same idiom seen in DMA/Timers/CDROM). PCSX-Redux keeps its real SPU implementation in a separate `src/spu/` module (mirroring this project's own `src/spu/` placement) and explicitly separates per-voice `ADSRInfo` (raw register values) from `ADSRInfoEx` (derived/precomputed runtime rates).

**Gaps**: None confirmed. Two verification items surfaced by cross-comparison, not confirmed defects:
1. Whether the 22050Hz-vs-44100Hz reverb resample buffer sizing matches the reference approach.
2. Whether register writes in `spu.c` flush pending samples before mutating state, matching the "flush before mutate" discipline this codebase already applies elsewhere (DMA/Timers).

**Priority**: Low — verification items only.

**Recommended action**: No SPU refactor required. Spot-check the flush-before-mutate ordering the next time `spu.c`'s register-write paths are touched for any other reason.

---

## 12. MDEC

**Files**: `src/core/mdec.c` (501L).

**Current state**: Explicitly ported from DuckStation's `mdec.cpp` (credited in the file header) — `IDCT_Old`/`DecodeRLE_Old`/`YUVToRGB_Old`/`YUVToMono`/`CopyOutBlock`, a real state machine (`IDLE`/`DECODING`/`WRITING`/`SET_QTABLE`/`SET_SCALE`/`NOCOMMAND`) mirroring DuckStation's `Execute()` loop, correct RLE/zigzag/quantization/2-pass-IDCT/YUV→RGB pipeline for both mono and color output depths. Not a stub — a working decoder, zero TODO markers.

**Reference pattern**: DuckStation offers two selectable IDCT/YUV code paths (this project ports only the "Old" — complete and correct on its own; "New" is a performance variant, not a correctness requirement) and paces decoded-block availability to the output FIFO via a dedicated `TimingEvent`. PCSX-Redux independently converges on the same overall shape (zigzag descan, AAN-scaled IDCT, YUV→RGB, 15/24-bit output) — cross-validating that this project's port is structurally sound.

**Gap**: None identified against either reference's design. One nuance not confirmed during this pass: whether decoded-block output is paced through the event scheduler (matching DuckStation's `block_copy_out_event`) or made available instantly — relevant to DMA timing accuracy during MDEC-heavy FMV playback.

**Priority**: Low — verification item only.

**Recommended action**: No refactor required. Verify block-output pacing semantics whenever the Phase 1 event-scheduler unification touches MDEC's `EVQ_MDEC` slot (which already has a real, non-`NULL` handler).

---

## 13. PCDRV

**Files**: `src/core/pcdrv.c` (194L).

**Current state**: Compiled into the binary, correctly implements the DuckStation-style host filesystem passthrough protocol (`PCinit/PCcreat/PCopen/PCclose/PCread/PCwrite/PClseek`, function codes 0x101-0x107 decoded from the MIPS `BREAK` instruction's 20-bit code field). **Zero call sites** exist for `PCDrv_HandleSyscall`/`_Initialize`/`_Reset`/`_Shutdown` outside `pcdrv.c` itself — `op_break` in `cpu_instructions.c` unconditionally raises `EXCEPTION_BREAK` without ever checking for a PCDrv code first.

**Reference pattern**: DuckStation intercepts exactly at this point — `RaiseBreakException` checks a `pcdrv_enable` setting and, if set, calls `PCDrv::HandleSyscall()` *before* raising the real exception; if handled, it short-circuits (advances PC, returns, no BIOS exception handler runs). Host filesystem access is sandboxed via path canonicalization + prefix check against a configured root (blocks `../` escapes — explicitly documented as traversal-prevention only, not a real sandbox). Writes are gated behind a separate `enable_writes` flag, off by default even when PCDrv itself is enabled.

**Gap**: This is a pure wiring gap, not an implementation gap — the interception point and mechanism already match the reference precisely; the function is simply never called.

**Priority**: Medium — zero risk to core emulation while inert, but a complete, correctly-implemented feature that's entirely unreachable is worth fixing cheaply. Flag: verify `pcdrv.c` already has path-sandboxing before enabling it by default; if not, add it as part of the same change (this is security-relevant, not purely structural, since it exposes host filesystem access to guest code once wired up).

**Recommended action**: Add the interception check at the top of `op_break` — decode the 20-bit BREAK code, and if it matches a PCDrv function-code range, call `PCDrv_HandleSyscall()` and short-circuit instead of always raising `EXCEPTION_BREAK`. Mirror DuckStation's off-by-default and read-only-by-default-even-when-enabled conventions.

---

## 14. Savestates

**Current state**: No savestate mechanism exists anywhere in the codebase — confirmed by absence across the full component inventory.

**Reference pattern**: DuckStation serializes each active `TimingEvent` by name, with subsystems' own `DoState()` recreating events before the scheduler patches in saved timing state. PCSX-Redux uses a custom compile-time reflection/schema system (`Protobuf::Field<Type, name, id>`) that auto-generates both wire format and field layout from a single declarative tree pointing directly at live runtime state — powerful, but requires machinery this C99 codebase doesn't have and isn't idiomatic here. That research pass's own recommendation for a plain-C99 project: skip the reflection layer, use a simple per-module `void X_save(X*, Buffer*)` / `X_load(...)` pair, with one top-level `emu_save_state()` calling each module's function in sequence.

**Gap**: complete absence of the feature — the largest single missing-feature (as opposed to structural-gap-in-existing-feature) finding in this audit.

**Priority**: High as a feature, but deliberately sequenced late — every other refactor phase changes struct shapes, so implementing savestates before those stabilize means rewriting save/load code repeatedly.

**Recommended action**: Defer to roadmap Phase 4, after Phases 0-3 stabilize struct shapes. Adopt the simple per-module save/load pattern; start with CPU/bus/RAM (smallest, most self-contained), expand outward to DMA/Timers/GPU/SPU/CDROM once those modules have already been cleaned up.

---

## Cross-Cutting Themes

- **Duplicate/dead code**: ~~`interrupt_controller.c/h` (fully dead)~~, dual event schedulers (CDROM ad-hoc array vs `event_scheduler.c` — still open, Phase 1), SIO's triple button-state copies (still open, Phase 2), ~~duplicated `TIMERS_*` defines in `interconnect.h`~~, ~~several dead/stub functions (`interconnect_debug_check_irq_status`, vestigial `bios_*` timer stubs)~~ — all struck-through items resolved in Phase 0; unreachable PCDrv still open (Phase 4).
- **Missing generic abstractions**: no `Controller` interface (root cause of the SIO duplication), no savestate mechanism, no GPU backend abstraction (hardware-only, no software fallback).
- **Test coverage gap**: `cpu_test` covers only CPU/bus/timers/GTE — no automated coverage exists for CDROM, SPU, SIO, or MDEC, despite those being among the most mature subsystems. Regressions there would go undetected by the existing test suite.
- **Not gaps, worth stating explicitly**: GTE, SPU, MDEC, CDROM, and GPU primitive coverage are all essentially reference-quality with zero-to-minimal findings. The refactor's real work is concentrated in Bus/IRQ, the Event Scheduler, SIO, DMA edge cases, and Timer edge cases — not a full rewrite of everything.

---

## Prioritized Refactor Roadmap

**Phase 0 — Cleanup (no behavior change)** ✅ **COMPLETE (2026-07-13)**
Lowest-risk, highest-clarity-per-effort. Verified via `make clean && make`: builds clean, no new warnings.
- [x] Delete `src/interrupt_controller.c` + `include/interrupt_controller.h` (§3)
- [x] Delete dead `interconnect_debug_check_irq_status()` (§3) and `interconnect_check_bios_boot()` (§2)
- [x] Fix duplicated `TIMERS_START/SIZE/END` defines in `interconnect.h` (§2)
- [x] Move `debugger.c` out of `src/gpu/` → `src/core/debugger.c`, Makefile updated (§8)
- [x] Remove vestigial commented-out HLE block + dead empty ifs in `op_jr`/`op_jalr` (§1)
- [x] Update stale "stub" comments in `event_scheduler.c` (§6)
- [x] Delete dead `bios_*` timer placeholder functions — 6 functions removed (§5)

**Phase 1 — Unify event scheduling**
The single highest-impact structural fix. Requires care: CDROM's current timing is calibrated and working.
- Migrate CDROM's private `CdromEvent` array (`interconnect.c`) onto the real `EVQ_CDROM`/`EVQ_DMA_CDROM` mechanism, using DuckStation's 4-event split (command/second-response/async-interrupt/drive) as the target shape (§6, §10)
- Audit rearm math for drift-correctness (`old_next_run_time + interval` vs `now + interval`) (§6)
- Remove or document the dead `EVQ_DMA_SPU`/`EVQ_DMA_OTC`/`EVQ_GPU` enum slots (§6)
- Regression-test against known-good BIOS boot + disc read before/after

**Phase 2 — SIO / Controller consolidation**
- Introduce a minimal C `Controller` abstraction (function-pointer struct, PCSX-Redux-`Pads`-style, not a class hierarchy) (§7)
- Collapse the three duplicate button-state copies down to one source of truth
- Wire memory card slot 2 through the same code path as slot 1, parametrized by index (§7)

**Phase 3 — DMA/Bus edge cases**
- Implement or explicitly document-as-intentional: DMA channel 5 (PIO) (§4)
- Verify/add infinite-loop guard on the GPU/OTC linked-list DMA walker (§4)
- Implement `SetRCnt` BIOS syscall handler (§5)
- Thread PAL/`VMode` flag into `timers_calculate_frame_cycles()` (§5)
- Remove hard-coded WATCHPOINT ranges from `bus.c`'s hot path, route through the existing `Debugger` mechanism (§2)
- Investigate top-level address routing (LUT vs switch) against PCSX-Redux's 64KB-granularity pattern (§2)

**Phase 4 — Savestates**
- Only after Phases 0-3 stabilize struct shapes, to avoid rewriting save/load code repeatedly
- Simple per-module `X_save`/`X_load` pattern, starting with CPU/bus/RAM, expanding outward (§14)
- Wire `op_break` to check for PCDrv codes before raising `EXCEPTION_BREAK`; verify/add path-sandboxing first (§13)

**Phase 5 — Optional/advanced (low priority, do only if there's a concrete reason)**
- GPU software rasterizer fallback / backend abstraction (§8)
- Analog controller / DualShock / rumble support, once the Controller abstraction from Phase 2 exists (§7)
- PGXP-equivalent precision geometry — explicitly optional per PCSX-Redux's own framing, not a core gap

---

## Changelog

**2026-07-13 — Phase 0 cleanup complete.** Files changed:
- Deleted: `src/interrupt_controller.c`, `include/interrupt_controller.h`
- Deleted (dead functions): `interconnect_debug_check_irq_status()` (`bus_irq.c` + `interconnect.h`), `interconnect_check_bios_boot()` (`bus.c`), 6× `bios_*` timer placeholder functions (`timers.c` + `timers.h`)
- Moved: `src/gpu/debugger.c` → `src/core/debugger.c` (`Makefile`: `EMU_GPU_SRCS` → `EMU_CORE_SRCS`)
- Edited: `interconnect.h` (removed duplicate `TIMERS_START/SIZE/END` block), `cpu_instructions.c` (removed dead code in `op_jr`/`op_jalr`), `event_scheduler.c` (updated stale comments)
- Verified: `make clean && make` builds clean, no new compiler warnings introduced. `make test` could not run — `tests/` directory absent from this checkout, pre-existing and unrelated to this change.
- Not yet done: everything else in Phases 1-5 below is still open.
