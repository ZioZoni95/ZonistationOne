-- Displayed-buffer content probe (post VRAM unification)
-- The renderer now shows exactly what is in VRAM at the CRTC display origin.
-- So: sample the CURRENTLY DISPLAYED rect and the other double-buffer half, to
-- see whether the FMV frame is in the buffer being shown, in the other one, or
-- nowhere. Answers "is the picture missing, or is the wrong buffer on screen?".

local n        = 0
local shown    = 0
local disp_hits, other_hits, both_black = 0, 0, 0

-- Peak byte over a coarse grid of a 24bpp rect (VRAM halfword coords).
local function peak24(x0, y0, w_hw, h)
    local peak = 0
    for row = 0, h - 1, 8 do
        for col = 0, w_hw - 1, 2 do
            local v = emu.vram16(x0 + col, y0 + row) or 0
            local lo, hi = v & 0xFF, (v >> 8) & 0xFF
            if lo > peak then peak = lo end
            if hi > peak then peak = hi end
        end
    end
    return peak
end

emu.on_event(function(name)
    if name ~= "gp0_vram_upload" then return end
    if ((emu.gpustat() >> 21) & 1) ~= 1 then return end   -- FMV only (24bpp)
    n = n + 1
    if (n % 60) ~= 0 then return end                      -- sample periodically

    local dx, dy = emu.display_area()
    local other  = (dy < 240) and 240 or 0                -- the other buffer half
    -- 320 display pixels of 24bpp = 480 halfwords
    local p_disp  = peak24(dx, dy,    480, 200)
    local p_other = peak24(dx, other, 480, 200)

    if p_disp > 40 then disp_hits = disp_hits + 1
    elseif p_other > 40 then other_hits = other_hits + 1
    else both_black = both_black + 1 end

    if shown < 12 then
        shown = shown + 1
        print(string.format("[BUF] cyc=%d disp=(%d,%d) peak_shown=0x%02X | other(y=%d) peak=0x%02X",
            emu.cycles(), dx, dy, p_disp, other, p_other))
    end
    if (n % 600) == 0 then
        print(string.format("[BUF-STATS] samples=%d | shown_has_image=%d wrong_buffer=%d both_black=%d",
            n // 60, disp_hits, other_hits, both_black))
    end
end)

print("[DISPLAYED-BUFFER PROBE] armed")
