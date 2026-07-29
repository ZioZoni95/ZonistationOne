-- ps_logo_probe_isolate.lua — isolation test: NO breakpoints at all, only
-- event counters (using the OLD, proven-working "gp0_poly" event as the
-- periodic print trigger, since it doesn't touch the new code path). If
-- this run also dies around cyc~186.8M, the bug is in the new
-- "gp0_poly_complete" probe/event itself, not in breakpoint handling.

local counts = {}
local gp0_poly_ticks = 0

emu.on_event(function(name)
    counts[name] = (counts[name] or 0) + 1
    if name == "gp0_poly" then
        gp0_poly_ticks = gp0_poly_ticks + 1
        if gp0_poly_ticks % 3000 == 0 then
            print(string.format("[cyc=%d] gp0_poly=%d gp0_rect=%d gp0_poly_complete=%d",
                  emu.cycles(), counts["gp0_poly"] or 0, counts["gp0_rect"] or 0,
                  counts["gp0_poly_complete"] or 0))
        end
    end
end)

print("ps_logo_probe_isolate.lua armed (no breakpoints).")
