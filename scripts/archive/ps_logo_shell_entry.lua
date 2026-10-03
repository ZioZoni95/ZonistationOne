-- ps_logo_shell_entry.lua — anchor the investigation at the REAL, documented
-- entry point of "the shell" (the RAM-resident binary loaded by the BIOS
-- kernel that displays the SONY sound + PS logo, per pcsx-redux openbios's
-- own README and DOCS/expansionportpio.md's "Mid-Boot Hook" trick): 0x80030000.
--
-- Log every entry (should fire once, at real shell start), then arm the
-- existing GTE-dispatch probe from that point on to see the very first
-- RTPS/RTPT call and its caller PC, unambiguously, instead of guessing.

local shell_entries = 0
local gte_count = 0
local armed = false

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if armed then
        gte_count = gte_count + 1
        if gte_count <= 15 then
            print(string.format("[gte #%d after shell entry] %s pc=0x%08x ra=0x%08x", gte_count, name, emu.pc(), emu.reg(31)))
        end
    end
end)

emu.on_break(function(reason)
    shell_entries = shell_entries + 1
    if shell_entries <= 5 then
        print(string.format("[shell entry #%d] pc=0x%08x ra=0x%08x sp=0x%08x", shell_entries, emu.pc(), emu.reg(31), emu.reg(29)))
    end
    armed = true
    emu.resume()
end)

print("ps_logo_shell_entry.lua armed — watching 0x80030000 (real shell entry point) + GTE dispatch after.")
