-- ps_logo_dump4.lua — full dump of the suspected real TMD-parsing routine
-- at 0x8004F784 (called via JAL from inside 0x8004f210's body; the read
-- watchpoint hit on the TMD buffer landed at PC=0x8004f804, inside this
-- function's range), plus the other helper at 0x80058890.

local dumped = false

emu.add_read_watch(0xA0010800)

emu.on_break(function(reason)
    if reason:find("Read watchpoint") and not dumped then
        dumped = true
        print(string.format("=== hit context: pc=0x%08x ra=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x s0=0x%08x s1=0x%08x ===",
              emu.pc(), emu.reg(31), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7), emu.reg(16), emu.reg(17)))
        print("=== word dump 0x8004f784-0x8004fa00 ===")
        for addr = 0x8004f784, 0x8004fa00 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
        print("=== word dump 0x80058890-0x80058a00 (other helper) ===")
        for addr = 0x80058890, 0x80058a00 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_dump4.lua armed.")
