-- ps_logo_colorword.lua — the logo flat-triangle prims have vertices but their
-- GP0 command word (opcode|colour) at prim+4 = 0, so the classify drops them
-- (opcode 0). Watch that word for one logo prim (0x800bbfc0) to learn why it's
-- zero: never written (setup skipped), written valid then cleared (double-
-- buffer clear bug), or written zero (colour source is zero). Log PC/value/cyc
-- for every write.

local L = emu.log
local n = 0

emu.add_write_watch(0x800bbfc0)   -- prim+4 (opcode|colour word) of a logo prim

emu.on_break(function(reason)
    n = n + 1
    if n <= 50 then
        L(string.format("[cyc=%d] write#%d 0x800bbfc0 <- 0x%08x  by pc=0x%08x ra=0x%08x  %s",
            emu.cycles(), n, emu.read_u32(0x800bbfc0), emu.pc(), emu.reg(31),
            emu.disasm(emu.pc())))
    end
    emu.resume()
end)

L("ps_logo_colorword.lua armed.")
