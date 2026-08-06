-- ps_logo_flag_verify.lua — resolve the contradiction: the RENDER_MODE word at
-- 0x8008A1B0 reads 0 (DMA) even during the logo, yet the software draw
-- 0x8005fd98 (reached only when the flag != 0) was observed firing. One of
-- those derivations is wrong. Verify DIRECTLY, no inference: at 0x8005ced8 read
-- the actual flag word AND t6 (the value it loads), and break at 0x8005cef8
-- (the jal 0x8005fd98 — only reached if the branch fell through, i.e. flag!=0)
-- to confirm which path is actually taken during the logo.

local L = emu.log
local a = 0
local b = 0

emu.add_breakpoint(0x8005ced8)   -- per-prim entry (loads flag into t6)
emu.add_breakpoint(0x8005cef8)   -- jal 0x8005fd98 (software draw) — only if flag!=0

emu.on_break(function(reason)
    local pc = emu.pc()
    local cyc = emu.cycles()
    if cyc < 186000000 then emu.resume(); return end

    if pc == 0x8005ced8 and a < 6 then
        a = a + 1
        L(string.format("[cyc=%d] 0x8005ced8 entry: flag word @0x8008A1B0 = 0x%08x  t6=0x%08x  a0(prim)=0x%08x",
            cyc, emu.read_u32(0x8008A1B0), emu.reg(14), emu.reg(4)))
    elseif pc == 0x8005cef8 and b < 6 then
        b = b + 1
        L(string.format("[cyc=%d] 0x8005cef8 REACHED -> software draw 0x8005fd98 taken (flag was NON-zero)  a1(prim)=0x%08x",
            cyc, emu.reg(5)))
    end
    emu.resume()
end)

L("ps_logo_flag_verify.lua armed.")
