-- ps_logo_shading_disasm.lua — disassemble 0x8005bbd8 (the shading helper
-- whose return value becomes t0 after >>2, and must be <1024 to pass the
-- gate that's currently blocking 100% of logo draws) using the real
-- disassembler, plus capture t9's actual value at the point it's read
-- (0x8004f668) to see its magnitude/pattern directly.

local dumped = false
local t9_samples = 0

emu.add_breakpoint(0x8004f668) -- right after return from 0x8005bbd8, t9 not yet loaded
emu.add_breakpoint(0x8004f674) -- t0 = t9>>2 already computed here

emu.on_break(function(reason)
    local pc = emu.pc()
    if pc == 0x8004f674 and t9_samples < 15 then
        t9_samples = t9_samples + 1
        local t9 = emu.reg(9)
        print(string.format("[cyc=%d] [t9 sample #%d] t9=0x%08x (%d) t0=0x%08x (%d)",
              emu.cycles(), t9_samples, t9, t9, emu.reg(8), emu.reg(8)))
    end
    if not dumped then
        dumped = true
        print("=== disasm 0x8005bbd8-0x8005bc80 (shading helper) ===")
        for addr = 0x8005bbd8, 0x8005bc80, 4 do
            print(string.format("%08x: %s", addr, emu.disasm(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_shading_disasm.lua armed.")
