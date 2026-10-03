-- ps_logo_gates_check.lua — check all 3 real gates found via correct
-- disassembly (emu.disasm), individually, to see which one is actually
-- responsible for the near-zero draw rate:
--   gate A (0x8004f674): blez t0   (t0 = shading result >> 2, must be > 0)
--   gate B (0x8004f680): slti at,t0,1024 / beq at,$0  (t0 must be < 1024)
--   gate C (0x8004f688): blez s0   (s0 = transform/clip result, must be > 0)
-- and whether the real draw call (0x8005f154) ever actually gets reached.

local a_total, a_pass = 0, 0
local b_total, b_pass = 0, 0
local c_total, c_pass = 0, 0
local draw_calls = 0

local function sign32(v)
    if v >= 0x80000000 then return v - 0x100000000 end
    return v
end

emu.add_breakpoint(0x8004f674) -- gate A: t0 in $8
emu.add_breakpoint(0x8004f688) -- gate C: s0 in $16 (only reached if A and B passed)
emu.add_breakpoint(0x8005f154) -- the real draw call entry

emu.on_break(function(reason)
    local pc = emu.pc()
    if pc == 0x8004f674 then
        a_total = a_total + 1
        local t0 = sign32(emu.reg(8))
        if t0 > 0 then
            a_pass = a_pass + 1
            if t0 < 1024 then b_pass = b_pass + 1 end
            b_total = b_total + 1
        end
    elseif pc == 0x8004f688 then
        c_total = c_total + 1
        local s0 = sign32(emu.reg(16))
        if s0 > 0 then c_pass = c_pass + 1 end
    elseif pc == 0x8005f154 then
        draw_calls = draw_calls + 1
    end

    if (a_total % 2000 == 0 and a_total > 0) or draw_calls == 1 or a_total == 20 then
        print(string.format("[cyc=%d] a_total=%d a_pass=%d | b_total=%d b_pass=%d | c_total=%d c_pass=%d | REAL_DRAW_CALLS=%d",
              emu.cycles(), a_total, a_pass, b_total, b_pass, c_total, c_pass, draw_calls))
    end
    emu.resume()
end)

print("ps_logo_gates_check.lua armed.")
