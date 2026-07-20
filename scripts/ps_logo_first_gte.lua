-- ps_logo_first_gte.lua — from the confirmed real shell entry (0x80030000),
-- find the FIRST actual GTE opcode dispatch (not the dma_ch2_done noise) and
-- report its PC/ra precisely, then stop (no runaway logging).

local armed = false
local found = false
local n = 0

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if not armed or found then return end
    n = n + 1
    if name ~= "dma_ch2_done" and name ~= "mdec_ch1_done" then
        found = true
        print(string.format("=== FIRST REAL GTE OP after shell entry: %s (event #%d) pc=0x%08x ra=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x ===",
              name, n, emu.pc(), emu.reg(31), emu.reg(4), emu.reg(5), emu.reg(6)))
    end
end)

emu.on_break(function(reason)
    if not armed then
        armed = true
        print(string.format("=== shell entered, pc=0x%08x, now watching for first real GTE op ===", emu.pc()))
    end
    emu.resume()
end)

print("ps_logo_first_gte.lua armed.")
