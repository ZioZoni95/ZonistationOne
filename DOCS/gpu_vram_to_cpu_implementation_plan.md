# Implementation Plan: GP0(0xC0) VRAM-to-CPU Transfer & GPUREAD Logic

## Goal
Enable BIOS menu rendering by fully implementing the PlayStation GPU VRAM-to-CPU transfer (GP0(0xC0)) and GPUREAD port logic.

---

## 1. GP0(0xC0) Command Handler
- **Parse parameters:** Extract X, Y, Width, Height from command buffer.
- **Calculate transfer region:** Clamp to VRAM bounds (1024x512).
- **Compute total pixels:** `total_pixels = width * height`.
- **Set up transfer state:**
  - Store region parameters in GPU struct.
  - Set `gp0_mode = GP0_MODE_IMAGE_STORE`.
  - Set `gp0_words_remaining = (total_pixels + 1) / 2` (2 pixels per word).
  - Reset pixel counter.

## 2. GPUREAD Port Implementation
- **When in IMAGE_STORE mode:**
  - On each read, return two 16-bit pixels from VRAM region as a 32-bit word.
  - Increment pixel counter, decrement `gp0_words_remaining`.
  - Handle odd pixel count (pad with zero).
  - When transfer complete, reset mode/state.
- **When not in IMAGE_STORE mode:**
  - Return dummy value or last status (as currently).

## 3. Edge Cases & Validation
- **Bounds checking:** Ensure X, Y, W, H do not exceed VRAM size.
- **Wrap-around:** Clamp pixel coordinates to VRAM (0x3FF, 0x1FF).
- **BIOS expectations:** Match transfer order and padding as per PSX docs.
- **Logging:** Log start, progress, and completion of transfer for debugging.

## 4. Testing & Verification
- **Run BIOS-only boot:** Confirm menu and font graphics appear.
- **Check logs:** Ensure GP0(0xC0) and GPUREAD are called and transfer correct data.
- **Compare with DuckStation:** Validate output and behavior against reference.

## 5. Optional Improvements
- **Performance:** Optimize pixel copying for large regions.
- **Compatibility:** Implement rare/undocumented variants if needed for games.
- **Unit tests:** Add tests for VRAM-to-CPU transfer logic.

---

## References
- DOCS/graphicsprocessingunitgpu.md
- DOCS/timers.md, DOCS/interrupts.md
- DuckStation src/core/gpu/
- PSX hardware documentation

---

**Priority:** This implementation is critical for BIOS menu rendering and should be completed before other GPU features.
