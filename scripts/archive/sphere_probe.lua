-- sphere_probe.lua — does the BIOS menu's 120x120 sphere survive in VRAM?
--
-- DuckStation's cpu_to_vram dump #60 is a 120x120 blue sphere; our trace shows
-- the matching A0 upload to (704,0) reporting 14400 of 14400 pixels written,
-- yet the region reads back black. This splits the two possibilities:
--   - black immediately after the upload  -> the write path never stored it
--   - non-black then black later          -> something overwrote it afterwards
-- Sampled every 4 px in both axes (30x30 = 900 taps) to stay off the hot path.

local X0, Y0, W, H = 704, 0, 120, 120

local function scan()
    local nz, peak = 0, 0
    for y = 0, H - 1, 4 do
        for x = 0, W - 1, 4 do
            local p = emu.vram16(X0 + x, Y0 + y) or 0
            if p ~= 0 then
                nz = nz + 1
                if p > peak then peak = p end
            end
        end
    end
    return nz, peak
end

local uploads, vb = 0, 0

emu.on_event(function(name)
    if name == "gp0_vram_upload" then
        uploads = uploads + 1
        local nz, peak = scan()
        emu.log(string.format(
            "[SPHERE] after upload #%d: page11 %d/900 non-zero peak=0x%04x cyc=%d",
            uploads, nz, peak, emu.cycles()))
    elseif name == "vblank" then
        vb = vb + 1
        if vb % 60 == 0 then
            local nz, peak = scan()
            emu.log(string.format(
                "[SPHERE] vblank %d: page11 %d/900 non-zero peak=0x%04x",
                vb, nz, peak))
        end
    end
end)
