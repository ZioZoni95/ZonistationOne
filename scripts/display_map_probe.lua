-- The whole VRAM-to-screen mapping, sampled live, printed only when it changes.
--
-- Answers the three questions that "the VRAM shows up instead of a black
-- screen" splits into, without a DEBUG run (a DEBUG run writes ~1.4M lines and
-- cannot be read):
--
--   1. Is the scanned rectangle taller than the picture?
--      Watch h against (le - ls). They must be equal unless the line says
--      480-lines, which needs BOTH interlace and vres=480
--      (psx-spx-docs/docs/graphicsprocessingunitgpu.md:772, :919-920).
--      h = 2*(le-ls) while the mode reads 240 means the display is scanning
--      twice the window, and the bottom half of the screen is unrelated VRAM.
--
--   2. Does the window sit where the frames land?
--      `up` is the last full-frame upload rectangle. If up.y is 8 while the
--      window y is 0, the top 8 lines on screen are stale VRAM — that is the
--      strip of macroblock noise, and the overscan crop used to hide it.
--
--   3. Is the black screen ours or the game's?
--      BLANK on the line is GPUSTAT.23, the display-disable bit
--      (GP1(03h).0, :906). Black expected + BLANK=0 means the game never asked
--      for black and something else is wrong; BLANK=1 while VRAM is on screen
--      means we are not honouring it.
--
-- Geometry only. Nothing here is a timing measurement, so it is safe to read
-- while playing — but do not quote a speed figure from a run with it loaded.
--
-- Run:  ZS1_LUA_SCRIPT=scripts/display_map_probe.lua ./ZoniStation_One <bios> --game=<bin>
-- Or load it from the Script panel (F9) while the game is running.

local vb   = 0
local prev = nil

-- Horizontal divider per resolution, for turning the GP1(06h) clock-unit range
-- into pixels: "(((X2-X1)/cycles_per_pix)+2) AND NOT 3" (:718-719). Dotclock
-- dividers are 10/8/5/4 for 256/320/512/640 and 7 for 368 (:1361-1367).
local function cycles_per_pixel(st)
    if (st >> 16) & 1 == 1 then return 7 end
    local hr1 = (st >> 17) & 3
    return ({ [0] = 10, [1] = 8, [2] = 5, [3] = 4 })[hr1]
end

emu.on_event(function(name)
    if name ~= "vblank" then return end
    vb = vb + 1

    local st = emu.gpustat()
    local vres480   = (st >> 19) & 1
    local pal       = (st >> 20) & 1
    local depth24   = (st >> 21) & 1
    local interlace = (st >> 22) & 1
    local blank     = (st >> 23) & 1

    local x, y, hs, he, ls, le = emu.display_area()
    local ux, uy, uw, uh = emu.vram_upload_rect()

    local cyc = cycles_per_pixel(st)
    local w = 0
    if he > hs then w = (((he - hs) // cyc) + 2) & ~3 end
    local lines = (le > ls) and (le - ls) or 0
    -- 480-lines mode is both bits, not interlace alone (:772).
    local h = lines
    if interlace == 1 and vres480 == 1 then h = h * 2 end

    local key = string.format("%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d",
        x, y, w, h, blank, depth24, interlace, vres480, pal, ux, uy)
    if key == prev then return end
    prev = key

    emu.log(string.format(
        "vb %d | win h=%d..%d (%d lines) v=%d..%d | out (%d,%d) %dx%d | " ..
        "up (%d,%d) %dx%d | %s %s %s %s%s",
        vb, hs, he, lines, ls, le,
        x, y, w, h,
        ux, uy, uw, uh,
        (pal == 1) and "PAL" or "NTSC",
        (interlace == 1) and "interlaced" or "progressive",
        (vres480 == 1) and "vres=480" or "vres=240",
        (depth24 == 1) and "24bpp" or "15bpp",
        (blank == 1) and " BLANK" or ""))

    -- Call out the two failures directly, so they do not have to be spotted by
    -- eye in a column of numbers.
    if h > lines and not (interlace == 1 and vres480 == 1) then
        emu.log(string.format(
            "  !! scanning %d lines for a %d-line window — the extra rows are VRAM", h, lines))
    end
    if uh >= 128 and uy ~= y then
        emu.log(string.format(
            "  !! frames land at y=%d, window starts at y=%d — %d stale lines on screen",
            uy, y, math.abs(uy - y)))
    end
end)
