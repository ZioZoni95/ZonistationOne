-- ps_logo_gte.lua — does the PS boot logo's drawing routine ever actually
-- invoke the GTE? (Web research: the real logo is a rotating 3D model,
-- transformed via GTE RTPS/RTPT, not flat static primitives — if GTE never
-- runs during this window, or runs and something's wrong with it, that would
-- explain degenerate/off-screen primitive coordinates regardless of the
-- counter/gate loop found earlier.)

local gte_count = 0
local first_pc, last_pc = nil, nil
local in_logo_window = false

emu.on_event(function(name)
    gte_count = gte_count + 1
    local pc = emu.pc()
    if not first_pc then first_pc = pc end
    last_pc = pc
    if gte_count <= 40 or (gte_count % 5000 == 0) then
        print(string.format("[gte #%d] %s pc=0x%08x", gte_count, name, pc))
    end
end)

-- Mark entry/exit of the known logo-gate PC range so we can tell whether any
-- GTE activity above overlaps with it.
emu.add_breakpoint(0x80067914)
emu.on_break(function(reason)
    if not in_logo_window then
        in_logo_window = true
        print(string.format("=== entered logo-gate window, gte_count so far=%d ===", gte_count))
    end
    emu.resume()
end)

print("ps_logo_gte.lua armed — logging all GTE opcode dispatches.")
