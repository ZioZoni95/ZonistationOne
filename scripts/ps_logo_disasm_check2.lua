-- ps_logo_disasm_check2.lua — dump REAL disassembly (emu.disasm) at the
-- correct moment in time: when the confirmed-firing breakpoint at
-- 0x8004f674 actually hits (deep into the logo's animation loop), not at
-- shell entry (too early — that RAM region isn't loaded with this code yet).

local done = false

emu.add_breakpoint(0x8004f674)

emu.on_break(function(reason)
    if not done then
        done = true
        for addr = 0x8004f630, 0x8004f6b0, 4 do
            print(string.format("%08x: %s", addr, emu.disasm(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_disasm_check2.lua armed.")
