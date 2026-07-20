-- ps_logo_rtpt_count.lua — count real GTE transform ops (RTPT/RTPS/NCLIP/etc)
-- from shell entry (0x80030000) onward, logging each one's caller PC, to see
-- whether the real per-vertex transform loop (JAL 0x8005c2b0 from 0x8004f648,
-- confirmed firing) runs the expected ~91+ times or stops early.

local armed = false
local gte_ops = 0
local dma_noise = 0
local seen_callers = {}

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if not armed then return end
    if name == "dma_ch2_done" or name == "mdec_ch1_done" then
        dma_noise = dma_noise + 1
        return
    end
    gte_ops = gte_ops + 1
    local ra = emu.reg(31)
    local key = string.format("0x%08x", ra)
    seen_callers[key] = (seen_callers[key] or 0) + 1
    if gte_ops <= 50 or gte_ops % 100 == 0 then
        print(string.format("[cyc=%d] [real gte op #%d] %s pc=0x%08x ra=0x%08x", emu.cycles(), gte_ops, name, emu.pc(), ra))
    end
end)

emu.on_break(function(reason)
    if not armed then
        armed = true
        print(string.format("[cyc=%d] === shell entered, counting real GTE ops (ignoring dma_ch2_done/mdec_ch1_done noise) ===", emu.cycles()))
    end
    emu.resume()
end)

print("ps_logo_rtpt_count.lua armed.")
