-- ps_logo_find_frame.lua — anchor on the exact PC from the reference
-- screenshot (0xBFC091AC, confirmed showing "PlayStation / Licensed by..."
-- text already rendering, logo model missing) and report the cycle count
-- so we can compute which real display frame to dump for a visual check.

emu.add_breakpoint(0xBFC091AC)

local hit = false

emu.on_break(function(reason)
    if not hit then
        hit = true
        print(string.format("[cyc=%d] hit 0xBFC091AC (reference screenshot PC)", emu.cycles()))
    end
    emu.resume()
end)

print("ps_logo_find_frame.lua armed.")
