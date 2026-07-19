-- mdec_handoff.lua
--
-- Investigates why the MDEC output -> GPU VRAM hand-off (ch1 done -> ch2
-- GPU-DMA reads the buffer) never triggers an 8th cycle during Ace Combat 2's
-- FMV intro. Confirmed so far: 7 clean ch1/ch2 pairs happen, then the game
-- only keeps re-triggering ch0 (same source address, unchanged) forever.
--
-- Counts native "mdec_ch1_done"/"dma_ch2_done" events (see the
-- lua_debug_notify() call sites in src/core/bus.c), logs the CPU PC at each,
-- and once the 7th hand-off completes, lets everything run silently until a
-- registered breakpoint/watchpoint fires — at which point it stops
-- auto-resuming so the Disassembly/Registers panels can be inspected by hand.

local count = 0
local watching = false

emu.on_event(function(name)
    if name == "mdec_ch1_done" or name == "dma_ch2_done" then
        count = count + 1
        print(string.format("[hand-off #%d] %s pc=0x%08x", count, name, emu.pc()))
    end
    if count == 7 and name == "dma_ch2_done" and not watching then
        print("=== 7th hand-off complete — arming watch, will halt on next break ===")
        watching = true
        -- Candidate: watch the MDEC ch0 source buffer itself (0x001bac04, per
        -- tonight's DMA.log analysis) for any write, in case the game re-fills
        -- it before re-sending, which would tell us it *is* progressing data.
        emu.add_write_watch(0x001bac04)
    end
end)

emu.on_break(function(reason)
    if not watching then
        -- Ignore incidental native breakpoints from other subsystems while
        -- we're still counting up to the 7th hand-off — keep running.
        emu.resume()
        return
    end
    print(string.format("[BREAK] %s pc=0x%08x ra=0x%08x", reason, emu.pc(), emu.reg(31)))
    -- Real halt from here — do NOT resume, drop into manual inspection.
end)

print("mdec_handoff.lua loaded — start FMV playback now.")
