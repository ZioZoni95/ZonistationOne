-- ps_logo_verify_fix.lua — verify the GTE CODE-byte fix. Before: logo Gouraud
-- prims went out with opcode 0 (CODE dropped by push_rgb_from_mac), got dropped
-- by the shell's software renderer, gp0_poly frozen ~884. After the fix the
-- prims should carry their real opcode (0x20/0x30 etc), pass the classify, and
-- reach GP0 -> gp0_poly should climb far past 884.

local L = emu.log
local ev = {}
local reported = 0

emu.on_event(function(name)
    ev[name] = (ev[name] or 0) + 1
    if name == "gp0_poly" and (ev.gp0_poly % 1000 == 0) and reported < 15 then
        reported = reported + 1
        L(string.format("[cyc=%d] gp0_poly=%d gp0_rect=%d",
            emu.cycles(), ev.gp0_poly or 0, ev.gp0_rect or 0))
    end
end)

L("ps_logo_verify_fix.lua armed.")
