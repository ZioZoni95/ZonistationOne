-- ps_logo_dump.lua — one-shot memory dump around the render call site
-- (0x80067888, ra of the tight loop calling 0x80067978 with static args)
-- so we can manually disassemble the loop condition without re-running.

local dumped = false

emu.add_breakpoint(0x80067978)

emu.on_break(function(reason)
    if not dumped then
        dumped = true
        print("=== word dump 0x80067840-0x800678C0 ===")
        for addr = 0x80067840, 0x800678C0 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
        print(string.format("=== regs at first hit: a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x ra=0x%08x s0=0x%08x s1=0x%08x s2=0x%08x ===",
              emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7), emu.reg(31), emu.reg(16), emu.reg(17), emu.reg(18)))
        print(string.format("RAM[0x8008ACFC]=0x%08x", emu.read_u32(0x8008ACFC)))
    end
    emu.resume()
end)

print("ps_logo_dump.lua armed.")
