-- ps_logo_primdump.lua — the logo prims (0x0bbxxx / 0x0bcxxx) are walked and
-- linked into the OT but their packet data reads gp0word=0 (dropped by the
-- classify). Dump a "dropped" logo prim packet fully vs a "valid" prim, to
-- confirm the logo packets are empty (never filled with GP0 command/coords/
-- colours). Then the bug is upstream in the primitive-build (GTE transform +
-- packet write), not the render path.

local L = emu.log
local dumped_bad = false
local dumped_ok = false

emu.add_breakpoint(0x8005fdb8)   -- classify point; a2=prim, v0=result

emu.on_break(function(reason)
    if emu.cycles() < 186000000 then emu.resume(); return end
    local a2 = emu.reg(6)
    local v0 = emu.reg(2)

    if v0 == 0 and not dumped_bad and a2 >= 0x800b0000 and a2 < 0x800d0000 then
        dumped_bad = true
        L(string.format("=== DROPPED logo prim @0x%08x (v0=0) full dump: ===", a2))
        for off = -8, 40, 4 do
            L(string.format("  [prim%+d] 0x%08x = 0x%08x", off, a2+off, emu.read_u32(a2+off)))
        end
    end
    if v0 ~= 0 and not dumped_ok then
        dumped_ok = true
        L(string.format("=== VALID prim @0x%08x (v0=%d) full dump for comparison: ===", a2, v0))
        for off = -8, 40, 4 do
            L(string.format("  [prim%+d] 0x%08x = 0x%08x", off, a2+off, emu.read_u32(a2+off)))
        end
    end
    if dumped_bad and dumped_ok then emu.pause() end
    emu.resume()
end)

L("ps_logo_primdump.lua armed.")
