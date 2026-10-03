-- ps_logo_disasm_check.lua — one-shot: print the REAL disassembly (via
-- emu.disasm, the actual disassembler, not hand-decoded hex) of the
-- instructions around the suspected draw/skip branch, to resolve a
-- contradiction found between two earlier scripts' results.

local done = false

emu.add_breakpoint(0x80030000)

emu.on_break(function(reason)
    if not done then
        done = true
        for addr = 0x8004f640, 0x8004f6a0, 4 do
            print(string.format("%08x: %s", addr, emu.disasm(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_disasm_check.lua armed.")
