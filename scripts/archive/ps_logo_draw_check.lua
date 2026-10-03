-- ps_logo_draw_check.lua — the GTE transform loop (RTPT/NCLIP/AVSZ3/NCDS from
-- ra=0x8004f650/0x8004f668) is confirmed running for ~166 frames. Does any
-- real GP0 polygon/rect draw command ever actually get issued during that
-- same window? If GTE runs but gp0_poly/gp0_rect never fires, the bug is
-- between GTE output and GP0 submission (e.g. the code never reads back
-- SXY/RGB and builds a GP0 packet). If gp0_poly DOES fire, the bug is in
-- the renderer/display path instead (draw offset, drawing area, VRAM
-- display-start, or the already-known Gap A/B renderer architecture gaps).

local armed = false
local gte_ops = 0
local poly_draws = 0
local rect_draws = 0
local other_draws = 0

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if not armed then return end
    if name == "dma_ch2_done" or name == "mdec_ch1_done" then return end
    if name == "gp0_poly" then
        poly_draws = poly_draws + 1
        if poly_draws <= 20 then
            print(string.format("[cyc=%d] [gp0_poly #%d] pc=0x%08x", emu.cycles(), poly_draws, emu.pc()))
        end
        return
    end
    if name == "gp0_rect" then
        rect_draws = rect_draws + 1
        if rect_draws <= 20 then
            print(string.format("[cyc=%d] [gp0_rect #%d] pc=0x%08x", emu.cycles(), rect_draws, emu.pc()))
        end
        return
    end
    gte_ops = gte_ops + 1
    if gte_ops == 1 or gte_ops % 2000 == 0 then
        print(string.format("[cyc=%d] [gte op #%d] %s poly_so_far=%d rect_so_far=%d",
              emu.cycles(), gte_ops, name, poly_draws, rect_draws))
    end
end)

emu.on_break(function(reason)
    if not armed then
        armed = true
        print(string.format("[cyc=%d] === shell entered, watching GTE ops + real GP0 poly/rect draws ===", emu.cycles()))
    end
    emu.resume()
end)

print("ps_logo_draw_check.lua armed.")
