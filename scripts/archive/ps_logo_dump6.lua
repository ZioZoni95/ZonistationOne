-- ps_logo_dump6.lua — dump the 3 functions called inside the confirmed
-- 91-iteration main loop (0x8004f728-0x8004f748: counter at RAM 0x80098770,
-- loops while < 0x5b=91), calling 0x8005cfa4 / 0x8005d05c / 0x8005cf50 each
-- pass — very likely per-vertex GTE-transform + draw. Looking for RTPS/RTPT
-- (COP2 "execute" format, top byte 0x4A/0x4B) in this range.

local dumped = false

emu.add_read_watch(0xA0010800)

emu.on_break(function(reason)
    if reason:find("Read watchpoint") and not dumped then
        dumped = true
        print("=== word dump 0x8005cf00-0x8005d180 ===")
        for addr = 0x8005cf00, 0x8005d180 - 4, 4 do
            print(string.format("%08x: %08x", addr, emu.read_u32(addr)))
        end
    end
    emu.resume()
end)

print("ps_logo_dump6.lua armed.")
