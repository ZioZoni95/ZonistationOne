-- ps_logo.lua — Phase 2.7: why does the PS boot logo only ever show partial
-- geometry (3/560 primitives)? Two candidate breakpoints from tonight's
-- earlier direct disassembly:
--   0x80067914 — the gate function: if (RAM[0x8008ACFC] < arg) draw else skip.
--                 Confirmed to alternate normally (draw/skip) for 200+ calls,
--                 so the gate ITSELF isn't the blocker.
--   0x80067978 — the actual render call, invoked only when the gate passes.
-- Question: does 0x80067978 get called repeatedly with ADVANCING arguments
-- (new primitives each time — logo should be progressively drawn, so the bug
-- would be elsewhere, e.g. rendering/VRAM) or with the SAME arguments every
-- time (stuck re-drawing the same 3 primitives — the bug is in whatever
-- advances the primitive index/pointer between calls)?

local gate_calls = 0
local render_calls = 0

emu.add_breakpoint(0x80067914)
emu.add_breakpoint(0x80067978)

emu.on_break(function(reason)
    local pc = emu.pc()
    if pc == 0x80067914 then
        gate_calls = gate_calls + 1
        if gate_calls <= 30 then
            print(string.format("[gate #%d] a0=0x%08x ra=0x%08x", gate_calls, emu.reg(4), emu.reg(31)))
        end
    elseif pc == 0x80067978 then
        render_calls = render_calls + 1
        print(string.format("[render #%d] a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x ra=0x%08x",
              render_calls, emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7), emu.reg(31)))
    end
    emu.resume()
end)

print("ps_logo.lua armed — watching 0x80067914 (gate) and 0x80067978 (render).")
