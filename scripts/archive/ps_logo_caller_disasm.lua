-- ps_logo_caller_disasm.lua — sample the REAL t9 (GPR index 25, not 9 —
-- 9 is t1; this was a bug in the earlier version of this script) at
-- 0x8004f674, alongside t0, to verify t0 == (t9 asr 2).

local samples = 0

emu.add_breakpoint(0x8004f674)

emu.on_break(function(reason)
    local pc = emu.pc()
    if pc == 0x8004f674 and samples < 20 then
        samples = samples + 1
        local ra = emu.reg(31)
        local t9 = emu.reg(25)
        local t0 = emu.reg(8)
        print(string.format("[cyc=%d] #%d ra=0x%08x t9=0x%08x t0=0x%08x (%d)",
              emu.cycles(), samples, ra, t9, t0, t0))
    end
    emu.resume()
end)

print("ps_logo_caller_disasm.lua (fixed t9=reg25) armed.")
