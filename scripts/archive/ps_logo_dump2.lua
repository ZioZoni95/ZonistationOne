-- ps_logo_dump2.lua — dump the middle of the gate function's body
-- (0x800678c0-0x80067930-ish) not covered by the first dump, looking for a
-- JAL to a real TMD-parse/GTE-transform/draw subroutine.

local dumped = false

emu.add_breakpoint(0x80067978)

emu.on_break(function(reason)
    if not dumped then
        dumped = true
        print("=== word dump 0x800678c0-0x80067990 ===")
        for addr = 0x800678c0, 0x80067990 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_dump2.lua armed.")
