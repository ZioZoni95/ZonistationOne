-- ps_logo_tmd_read2.lua — follow-up: log the first ~30 reads of the TMD
-- buffer (KSEG1, confirmed via ps_logo_tmd_read.lua) to see the access
-- pattern, and dump memory around the confirmed PC (0x8004f804) so we can
-- disassemble the real TMD-consuming routine by hand.

local hits = 0
local dumped = false

emu.add_read_watch(0xA0010800)

emu.on_break(function(reason)
    if reason:find("Read watchpoint") then
        hits = hits + 1
        if hits <= 30 then
            print(string.format("[TMD-read #%d] pc=0x%08x ra=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x",
                  hits, emu.pc(), emu.reg(31), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7)))
        end
        if not dumped then
            dumped = true
            print("=== word dump 0x8004f200-0x8004f8a0 (around PC/ra) ===")
            for addr = 0x8004f200, 0x8004f8a0 - 4, 4 do
                print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
            end
        end
    end
    emu.resume()
end)

print("ps_logo_tmd_read2.lua armed.")
