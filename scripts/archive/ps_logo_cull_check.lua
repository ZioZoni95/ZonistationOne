-- ps_logo_cull_check.lua — 0 out of 8000+ positive-NCLIP passes ever led to
-- a GP0 draw (scripts/ps_logo_correlate.lua). Found the likely culprit by
-- re-reading yesterday's already-captured disassembly dump (no new dump
-- needed): right after the shading-computation call returns (ra=0x8004f668),
-- the very next real instructions are:
--   8004f668: lw   $25, 0xa0($sp)
--   8004f670: sra  $8, $25, 1
--   8004f674: blez $8, <skip-forward>      <-- suspected draw/skip decision
-- Breakpoint exactly there and read $8 (and $25 before the shift) directly
-- to see if it's suspiciously always <= 0 (which would explain a 100% skip
-- rate regardless of the actual NCLIP/shading result).

local armed = false
local hits = 0
local skip = 0
local draw = 0

emu.add_breakpoint(0x8004f674)

emu.on_break(function(reason)
    hits = hits + 1
    local r8 = emu.reg(8)
    -- sign-extend 32-bit value for correct comparison in Lua
    if r8 >= 0x80000000 then r8 = r8 - 0x100000000 end
    if r8 <= 0 then skip = skip + 1 else draw = draw + 1 end
    if hits <= 20 or hits % 2000 == 0 then
        print(string.format("[cyc=%d] [cull-check #%d] $8=%d (skip=%d draw=%d) $25(raw)=0x%08x",
              emu.cycles(), hits, r8, skip, draw, emu.reg(25)))
    end
    emu.resume()
end)

print("ps_logo_cull_check.lua armed — breakpoint at 0x8004f674 (suspected draw/skip decision).")
