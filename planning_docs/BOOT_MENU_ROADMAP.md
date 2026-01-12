# Roadmap to Interactive BIOS Boot Menu

## Goal: Match DuckStation's Boot Menu Display

Target: Display "MAIN MENU", "MEMORY CARD", "CD PLAYER" with animated bubbles, no duplication.

## Priority Fixes (In Order):

### 1. Fix Screen Duplication ⚠️ CRITICAL
**Problem:** Both interlaced fields showing same content at different Y offsets

**Root Cause:** `renderer_blit_vram()` renders the entire VRAM region without considering which field should be displayed.

**Solution:** Disable interlacing for now (simplest fix)
- Change GP1(08h) handler to ignore bit 5 (interlace enable)
- Force `interlaced = false` in GPU state
- This makes the BIOS render in progressive mode (non-interlaced)

**Alternative:** Implement proper interlaced rendering
- In renderer, only blit odd/even scanlines based on `interlaced_display_field`
- Requires modifying the VRAM blit to skip lines

**Priority:** FIX FIRST - Screen must be readable

### 2. Fix CDROM INT5 Error Loop 🔴 BLOCKING
**Problem:** CDROM command 0x19 (Test) triggers INT5 (error) repeatedly

**Root Cause:** CDROM stub returns INT5 for all commands when no disc present

**Solution:** Properly handle "no disc" state
- Command 0x19 (Test) with 0x20 parameter = GetID
- Should return INT3 (success) with stat=0x10 (ShellOpen, no disc)
- NOT INT5 (error)

**Files to modify:**
- `src/cdrom/cdrom_core.c`: Fix command 0x19 handler
- Return proper status byte: bit 4 = ShellOpen (no disc)
- Use INT3 instead of INT5

**Priority:** FIX SECOND - Stops error spam, lets menu load

### 3. Implement Basic Controller Reading 🎮 OPTIONAL
**Problem:** Can't navigate menu without controller input

**Solution:** Stub controller functions to return "no button pressed"
- B(12h) InitPAD2 - return success
- B(13h) StartPAD2 - return success
- B(15h) PAD_init2 - set buffer to 0xFFFF (no buttons)
- B(16h) PAD_dr - return 0 (ready)

**Priority:** FIX THIRD - Makes menu navigable

---

## Quick Wins (30 minutes each):

### Win #1: Disable Interlacing
```c
// In src/gpu/gpu_core.c or wherever GP1(08h) is handled:
// When bit 5 of GP1(08h) parameter is set (interlace enable):
gpu->interlaced = false;  // Force progressive mode
```

### Win #2: Fix CDROM GetID Command
```c
// In src/cdrom/cdrom_core.c, command 0x19 handler:
if (param == 0x20) {  // GetID/Test
    // No disc response:
    cdrom->response[0] = 0x10;  // Stat: ShellOpen, no disc
    cdrom->response[1] = 0x00;  // No additional data
    cdrom->response_length = 1;
    
    // Trigger INT3 (success/acknowledge), NOT INT5 (error)
    cdrom_trigger_interrupt(cdrom, 3);  // INT3
}
```

### Win #3: Stub Controller
```c
// In src/bios.c, add to BIOS function handlers:
case 0x12: // B(12h) InitPAD2
    cpu_set_reg(cpu, REG_V0, 1);  // Return success
    break;

case 0x15: // B(15h) PAD_init2
    // Write 0xFFFF to button_dest buffer (no buttons pressed)
    uint32_t buf_addr = cpu_reg(cpu, REG_A1);
    interconnect_store16(cpu->inter, buf_addr, 0xFFFF);
    cpu_set_reg(cpu, REG_V0, 1);
    break;
```

---

## Expected Result After Fixes:

1. **No screen duplication** - Single clean image
2. **No CDROM error spam** - Menu loads without INT5 loops
3. **Menu displays** - "MAIN MENU", "MEMORY CARD", "CD PLAYER" visible
4. **Optional: Menu navigable** - Can select with controller (if implemented)

---

## Test Plan:

1. Apply fix #1 (disable interlacing)
2. Build: `make`
3. Run: `./myps1_emu --gpu-thread roms/SCPH1001.BIN`
4. **Expected:** Logo appears ONCE (not duplicated)

5. Apply fix #2 (CDROM GetID)
6. Rebuild and run
7. **Expected:** No INT5 errors in log, menu appears

8. Apply fix #3 (controller stubs)
9. Rebuild and run
10. **Expected:** Menu fully functional (though not navigable yet)

---

## What You DON'T Need (For Boot Menu Only):

- ❌ Full CDROM drive emulation
- ❌ ISO/CUE file loading
- ❌ Memory card save/load
- ❌ SPU audio playback
- ❌ GTE coprocessor
- ❌ DMA timing accuracy
- ❌ Full controller protocol

## What You DO Need:

- ✅ Fix screen duplication
- ✅ Fix CDROM "no disc" response
- ✅ Basic controller stubs (optional for interactivity)

---

## Next Step After Boot Menu Works:

Once the menu displays properly, you can choose:
- **Option A:** Implement controller input to navigate menu
- **Option B:** Skip menu, implement game loading directly
- **Option C:** Implement Memory Card manager functions

For now: **Focus on the 3 quick wins above to get a clean boot menu display.**
