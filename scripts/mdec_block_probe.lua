-- FMV VRAM peak-tracker (Phase 2.8)
-- Tracks the brightest 24bpp byte written into the FMV region over the whole
-- movie. If it stays near 0 the output is being clamped/lost (real bug); if it
-- climbs, the earlier black screen was just a dark opening frame.

local uploads = 0
local global_peak = 0
local peak_cyc = 0

emu.on_event(function(name)
    if name ~= "gp0_vram_upload" then return end
    if ((emu.gpustat() >> 21) & 1) ~= 1 then return end   -- FMV only (24bpp)
    uploads = uploads + 1

    local x, y, w, h = emu.vram_upload_rect()
    local peak = 0
    for row = 0, h - 1, 8 do
        for col = 0, w - 1 do
            local v = emu.vram16(x + col, y + row) or 0
            local lo, hi = v & 0xFF, (v >> 8) & 0xFF
            if lo > peak then peak = lo end
            if hi > peak then peak = hi end
        end
    end
    if peak > global_peak then global_peak = peak; peak_cyc = emu.cycles() end

    if uploads % 400 == 0 then
        print(string.format("[VRAM-PEAK] %d uploads, cyc=%d: global peak byte=0x%02X (%d) @cyc %d",
            uploads, emu.cycles(), global_peak, global_peak, peak_cyc))
    end
end)

print("[FMV VRAM PEAK PROBE] armed")
