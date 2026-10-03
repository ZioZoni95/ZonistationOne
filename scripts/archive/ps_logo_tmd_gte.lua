-- ps_logo_tmd_gte.lua — does the real TMD-consuming function (confirmed
-- entry 0x8004f210, found via read-watchpoint on the TMD buffer) invoke the
-- GTE? Combines an exec breakpoint at the function entry with the GTE
-- opcode-dispatch event probe.

local entries = 0
local gte_since_entry = 0
local gte_total = 0

emu.add_breakpoint(0x8004f210)

emu.on_event(function(name)
    gte_total = gte_total + 1
    if entries > 0 then
        gte_since_entry = gte_since_entry + 1
        if gte_since_entry <= 20 then
            print(string.format("[gte after entry #%d] %s pc=0x%08x", gte_since_entry, name, emu.pc()))
        end
    end
end)

emu.on_break(function(reason)
    entries = entries + 1
    if entries <= 15 then
        print(string.format("[entry #%d] 0x8004f210 a0=0x%08x a1=0x%08x a2=0x%08x ra=0x%08x gte_total_so_far=%d",
              entries, emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(31), gte_total))
    end
    emu.resume()
end)

print("ps_logo_tmd_gte.lua armed — watching 0x8004f210 entries + all GTE dispatches.")
