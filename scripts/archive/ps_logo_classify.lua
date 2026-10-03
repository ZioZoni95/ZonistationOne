-- ps_logo_classify.lua — TARGET 2. 0x8005fd98 reads the prim's GP0 opcode
-- (lbu a0,7(prim)), classifies it via 0x8005fb00, and at 0x8005fdb8 does
-- `beq v0,$0,skip` — if the classify returns 0 the primitive is DROPPED (never
-- dispatched to a handler, never emitted). Capture, per logo prim: the opcode
-- byte, the classify result v0, and (if non-zero) the dispatched handler t2 at
-- 0x8005fe0c. If v0==0 for the logo prims, that's where they die.

local L = emu.log
local n = 0
local m = 0

emu.add_breakpoint(0x8005fdb8)   -- after classify: v0 = result, a2 = prim
emu.add_breakpoint(0x8005fe0c)   -- dispatch: jalr t2 (handler)

emu.on_break(function(reason)
    if emu.cycles() < 186000000 then emu.resume(); return end
    local pc = emu.pc()

    if pc == 0x8005fdb8 and n < 16 then
        n = n + 1
        local a2 = emu.reg(6)              -- prim pointer
        local w1 = emu.read_u32(a2 + 4)    -- first GP0 word
        local op = emu.read_u8(a2 + 7)     -- byte 7 = GP0 opcode
        L(string.format("[cyc=%d] classify: prim=0x%08x gp0word=0x%08x opcode=0x%02x -> v0=%d %s",
            emu.cycles(), a2, w1, op, emu.reg(2),
            emu.reg(2) == 0 and "  <== DROPPED (classify returned 0)" or ""))
    elseif pc == 0x8005fe0c and m < 8 then
        m = m + 1
        L(string.format("[cyc=%d] dispatch: handler t2=0x%08x  a0=0x%08x a1=0x%08x a3=0x%08x",
            emu.cycles(), emu.reg(25), emu.reg(4), emu.reg(5), emu.reg(7)))
    end
    emu.resume()
end)

L("ps_logo_classify.lua armed.")
