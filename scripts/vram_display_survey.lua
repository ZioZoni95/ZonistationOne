-- Where everything lives in VRAM, and what the display window actually covers,
-- over a 120-second run.
--
-- Three streams, at three rates, so the log stays readable:
--
--   per change   the display window and mode, exactly as gpu_update_display_mapping()
--                computed it (change-only, so a steady scene is silent)
--   every ~1 s   one summary line: how much of VRAM holds content, and whether
--                the lines just above and just inside the window are black or stale
--   every ~5 s   the whole 1024x512 VRAM as a 64x32 tile map
--
-- The map's characters, from emu.vram_map():
--   '.'  all zero — never written
--   '-'  uniform non-zero — a fill or a cleared buffer
--   ':'  mostly uniform with some variation
--   '#'  varied — a picture, a texture page, a decoded frame
--
-- The tile map is read from the GPU readback when one has landed, so rasterised
-- polygons are in it; the CPU-side mirror only ever sees uploads, fills and DMA.
-- Each line says which source it used. A '.' region in a 'cpu' map means only
-- "nothing was uploaded here", not "nothing is drawn here".
--
-- Geometry and occupancy only. Do not quote a speed figure from a run with this
-- loaded — the 5-second tile scan walks all 524288 halfwords.
--
-- Run:  ZS1_LUA_SCRIPT=scripts/vram_display_survey.lua ./ZoniStation_One <bios> --game=<bin>

local TILE      = 16     -- tile size, so the map is 64x32
local SUMMARY   = 50     -- vblanks between summary lines (~1 s at PAL)
local MAP_EVERY = 250    -- vblanks between full maps (~5 s at PAL)
local STOP_AT   = 6000   -- vblanks; ~120 s at PAL, ~100 s at NTSC

local vb        = 0
local prev_mode = nil
local stopped   = false

local function mode_of()
    local st = emu.gpustat()
    return {
        vres480   = (st >> 19) & 1,
        pal       = (st >> 20) & 1,
        depth24   = (st >> 21) & 1,
        interlace = (st >> 22) & 1,
        blank     = (st >> 23) & 1,
        hr2       = (st >> 16) & 1,
        hr1       = (st >> 17) & 3,
    }
end

local function cycles_per_pixel(m)
    if m.hr2 == 1 then return 7 end
    return ({ [0] = 10, [1] = 8, [2] = 5, [3] = 4 })[m.hr1]
end

-- The window as the hardware defines it: width from the GP1(06h) range
-- (:718-719), height from the GP1(07h) range with no rounding (:756-758), and
-- doubled only in 480-lines mode, which is interlace AND vres=480 (:772, :919-920).
local function window()
    local m = mode_of()
    local x, y, hs, he, ls, le = emu.display_area()
    local cyc = cycles_per_pixel(m)
    local w = 0
    if he > hs then w = (((he - hs) // cyc) + 2) & ~3 end
    local h = (le > ls) and (le - ls) or 0
    if m.interlace == 1 and m.vres480 == 1 then h = h * 2 end
    return m, x, y, w, h
end

-- One row of the window, classified. In 24bpp a pixel is 3 bytes, so a row of w
-- pixels spans w*3/2 halfwords (:1167-1169) — using w directly would sample only
-- two thirds of the line.
local function row_kind(y, x, w, depth24)
    if y < 0 or y > 511 then return "oob" end
    local halfwords = depth24 == 1 and (w * 3) // 2 or w
    if halfwords < 1 then return "empty" end
    local nonzero, differing = emu.vram_row_stats(y, x, halfwords)
    if nonzero == nil then return "oob" end
    if nonzero == 0 then return "black" end
    if differing == 0 then return "flat" end
    return string.format("content(%d/%d)", nonzero, halfwords)
end

emu.on_event(function(name)
    if name ~= "vblank" or stopped then return end
    vb = vb + 1

    local m, x, y, w, h = window()

    -- 1. the window, whenever it changes
    local key = string.format("%d/%d/%d/%d/%d/%d/%d/%d/%d",
        x, y, w, h, m.blank, m.depth24, m.interlace, m.vres480, m.pal)
    if key ~= prev_mode then
        prev_mode = key
        emu.log(string.format(
            "vb %d | window (%d,%d) %dx%d | %s %s %s %s%s",
            vb, x, y, w, h,
            (m.pal == 1) and "PAL" or "NTSC",
            (m.interlace == 1) and "interlaced" or "progressive",
            (m.vres480 == 1) and "vres=480" or "vres=240",
            (m.depth24 == 1) and "24bpp" or "15bpp",
            (m.blank == 1) and " BLANK" or ""))
    end

    -- 2. once a second: occupancy, and the rows around the top edge of the window
    if vb % SUMMARY == 0 then
        local map, cols, rows, src = emu.vram_map(TILE, TILE)
        local empty, flat, mixed, full = 0, 0, 0, 0
        for i = 1, #map do
            local c = map:sub(i, i)
            if     c == "." then empty = empty + 1
            elseif c == "-" then flat  = flat  + 1
            elseif c == ":" then mixed = mixed + 1
            else                 full  = full  + 1 end
        end
        emu.log(string.format(
            "vb %d | vram %s: %d empty %d flat %d mixed %d content (of %d tiles) | " ..
            "above=%s top=%s mid=%s bottom=%s",
            vb, src, empty, flat, mixed, full, cols * rows,
            row_kind(y - 1, x, w, m.depth24),
            row_kind(y, x, w, m.depth24),
            row_kind(y + h // 2, x, w, m.depth24),
            row_kind(y + h - 1, x, w, m.depth24)))
    end

    -- 3. every five seconds: the whole of VRAM
    if vb % MAP_EVERY == 0 then
        local map, cols, rows, src = emu.vram_map(TILE, TILE)
        emu.log(string.format("vb %d | VRAM map %dx%d tiles of %dx%d, from %s | " ..
                              "window (%d,%d) %dx%d = tiles x %d..%d y %d..%d",
                              vb, cols, rows, TILE, TILE, src, x, y, w, h,
                              x // TILE, (x + math.max(w, 1) - 1) // TILE,
                              y // TILE, (y + math.max(h, 1) - 1) // TILE))
        for r = 0, rows - 1 do
            emu.log(string.format("  y%3d |%s|", r * TILE, map:sub(r * cols + 1, (r + 1) * cols)))
        end
    end

    if vb >= STOP_AT then
        stopped = true
        emu.log(string.format("vb %d | survey complete (%d vblanks) — sampling stopped", vb, vb))
    end
end)
