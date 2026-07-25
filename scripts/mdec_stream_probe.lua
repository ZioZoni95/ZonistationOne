-- Where does the decoded FMV go? (post timer-unification)
-- The decoder now produces bright blocks (peak luma ~189) but the screen is
-- black. This traces the hand-off: for each GP0(0xA0) upload during the FMV,
-- compare the ch1 (MDEC-out) DMA destination against the upload source region,
-- and report the peak byte actually landing in VRAM. If VRAM stays ~0 while the
-- decoder is bright, the bright output never reaches the uploaded region.

local uploads     = 0
local vram_peak   = 0
local vram_peak_c = 0

emu.on_event(function(name)
    if name ~= "gp0_vram_upload" then return end
    if ((emu.gpustat() >> 21) & 1) ~= 1 then return end   -- FMV only (24bpp)
    uploads = uploads + 1

    local x, y, w, h = emu.vram_upload_rect()
    local peak = 0
    for row = 0, h - 1, 4 do
        for col = 0, w - 1 do
            local v = emu.vram16(x + col, y + row) or 0
            local lo, hi = v & 0xFF, (v >> 8) & 0xFF
            if lo > peak then peak = lo end
            if hi > peak then peak = hi end
        end
    end
    if peak > vram_peak then vram_peak = peak; vram_peak_c = emu.cycles() end

    if uploads % 300 == 0 then
        local in_a, in_r, out_a, out_r = emu.mdec_dma()
        print(string.format("[VRAM-DEST] %d uploads | rect=(%d,%d) %dx%d | VRAM peak byte=0x%02X @cyc %d | ch1_out=0x%06X",
            uploads, x, y, w, h, vram_peak, vram_peak_c, out_a))
    end
end)

print("[VRAM DEST PROBE] armed")
