-- ps_logo_rendermode_watch.lua — TARGET 1: the RENDER_MODE flag at 0x8008A1B0
-- selects DMA (0) vs software (!=0) rendering. It is 0 at early boot, !=0
-- during the logo (software path, which never reaches GP0). Watch every write
-- to it: PC, value, cycle. Find what flips it to software mode and whether that
-- write is driven by a value our emulation gets wrong (if it SHOULD stay 0/DMA,
-- the DMA path would draw the logo OT correctly).

local L = emu.log
local n = 0

emu.add_write_watch(0x8008A1B0)

emu.on_break(function(reason)
    n = n + 1
    if n <= 40 then
        L(string.format("[cyc=%d] write#%d RENDER_MODE(0x8008A1B0) <- 0x%08x  by pc=0x%08x ra=0x%08x",
            emu.cycles(), n, emu.read_u32(0x8008A1B0), emu.pc(), emu.reg(31)))
    end
    emu.resume()
end)

L("ps_logo_rendermode_watch.lua armed.")
