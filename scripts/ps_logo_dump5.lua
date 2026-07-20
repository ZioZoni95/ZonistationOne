-- ps_logo_dump5.lua — dump 0x8004f210's body AFTER its calls to the GTE
-- matrix-setup helper (0x80058890) and the primitive-unpack helper
-- (0x8004F784), looking for the per-vertex transform loop (RTPS/RTPT call)
-- or whatever condition might be skipping it. ra=0x8004f260 was the known
-- return site from the SECOND call; dump forward from there.

local dumped = false

emu.add_read_watch(0xA0010800)

emu.on_break(function(reason)
    if reason:find("Read watchpoint") and not dumped then
        dumped = true
        print("=== word dump 0x8004f260-0x8004f784 (between the two known calls / after) ===")
        for addr = 0x8004f260, 0x8004f784 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_dump5.lua armed.")
