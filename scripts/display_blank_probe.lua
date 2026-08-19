-- Why the screen goes black across a scene change.
--
-- GPUSTAT.23 is the display-off bit (GP1(03).0, DOCS/graphicsprocessingunitgpu.md:640-648).
-- Samples it once per vblank together with the display window and the depth, and
-- logs only the transitions: how many fields the display stayed off, and what the
-- CRTC was pointed at while it was off. A blank that lasts one or two fields is a
-- game hiding a framebuffer swap; one that lasts dozens is ours to explain.
local vb        = 0
local prev      = nil
local since     = 0
local off_start = 0
local mb        = 0   -- macroblocks decoded since the last line printed

emu.on_event(function(name)
    if name == "mdec_macroblock" then mb = mb + 1; return end
    if name ~= "vblank" then return end
    vb = vb + 1

    local st    = emu.gpustat()
    local blank = (st >> 23) & 1
    local d24   = (st >> 21) & 1
    local x, y, hs, he, ls, le = emu.display_area()

    if prev == nil then
        prev, off_start = blank, vb
    elseif blank ~= prev then
        if blank == 1 then
            off_start = vb
            emu.log(string.format(
                "vb %d: display OFF   win=(%d,%d) h=%d..%d l=%d..%d d24=%d",
                vb, x, y, hs, he, ls, le, d24))
        else
            emu.log(string.format(
                "vb %d: display ON  after %d fields off   win=(%d,%d) l=%d..%d d24=%d",
                vb, vb - off_start, x, y, ls, le, d24))
        end
        prev = blank
    end

    -- While off, report every 30th field so a stuck blank is visible in the log
    -- rather than inferred from the absence of a matching ON line.
    if blank == 1 then
        since = since + 1
        if since % 30 == 0 then
            emu.log(string.format("vb %d: still OFF (%d fields)   win=(%d,%d)",
                                  vb, vb - off_start, x, y))
        end
    else
        since = 0
    end
end)
