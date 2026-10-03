-- ps_logo_dump3.lua — dump the newly-found subroutine at 0x800684a0,
-- called from inside the gate function's draw branch (0x8006795c),
-- looking for COP2/GTE opcodes (MTC2/CTC2/GTE-command words) that would
-- confirm this is the real TMD-vertex-transform-and-draw routine.

local dumped = false

emu.add_breakpoint(0x80067978)

emu.on_break(function(reason)
    if not dumped then
        dumped = true
        print("=== word dump 0x800684a0-0x80068600 ===")
        for addr = 0x800684a0, 0x80068600 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_dump3.lua armed.")
