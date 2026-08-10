-- Which VRAM rows the FMV actually writes each field, against the window the
-- CRTC is scanning out.
--
-- The magenta/green strip along the top of the movie is stale VRAM read with the
-- 24bpp unpack: something in the display window was never written this field.
-- This prints, per field, the union of the GP0(A0) upload rects and the display
-- window, so a row band present in the window but absent from the uploads is
-- visible directly instead of inferred.
local vb    = 0
local n     = 0        -- upload rects this field
local ymin, ymax, xmin, xmax
local hw    = 0        -- halfwords uploaded this field

emu.on_event(function(name)
    if name == "gp0_vram_upload" then
        local x, y, w, h = emu.vram_upload_rect()
        n  = n + 1
        hw = hw + w * h
        if not ymin or y < ymin then ymin = y end
        if not ymax or y + h > ymax then ymax = y + h end
        if not xmin or x < xmin then xmin = x end
        if not xmax or x + w > xmax then xmax = x + w end
        return
    end
    if name ~= "vblank" then return end
    vb = vb + 1

    if n > 0 then
        local st  = emu.gpustat()
        local d24 = (st >> 21) & 1
        local dx, dy, _, _, ls, le = emu.display_area()
        local rows = le - ls
        -- Display window in VRAM halfwords: 24bpp reads 3 halfwords per 2 pixels.
        local wpx  = 320
        local whw  = (d24 == 1) and (wpx * 3 // 2) or wpx
        emu.log(string.format(
            "vb %d: uploads=%d hw=%d rect=(%d..%d, %d..%d)  window=(%d..%d, %d..%d) d24=%d",
            vb, n, hw, xmin, xmax, ymin, ymax,
            dx, dx + whw, dy, dy + rows, d24))
    end
    n, hw, ymin, ymax, xmin, xmax = 0, 0, nil, nil, nil, nil
end)
