-- recenter_watch.lua — every change of the displayed area, with what the audio
-- was doing at that instant.
--
-- The picture is reported to jump ("re-centre") at the PlayStation logo and
-- several times again during play, with an audible cut each time. Both would
-- follow from one cause, so the two are printed on the same line: the six CRTC
-- fields that decide what is shown, and the ring level, underrun count and the
-- vblank's wallclock gap that decide whether anything was heard.
--
-- One line per change, not per frame, so a whole session is a handful of lines.

local prev, vb, last_ms = nil, 0, nil
-- CD audio leaving the mix is the audible cut, and how long it lasts is decided
-- by what the drive is doing meanwhile. Sector delivery is sampled every vblank
-- so the rate inside a mute window can be read off, not guessed: a drive still
-- streaming means the game chose the silence, a drive delivering nothing means
-- it is waiting for us.
local prev_sect, mute_vb, mute_sect = nil, nil, 0
local prev_ctrl = nil

    local now  = emu.host_ms()
    local gap  = last_ms and (now - last_ms) or 0
    last_ms = now

    local _, _, _, _, _, ctrl_now, sect = emu.cd_audio()
    if prev_sect == nil then prev_sect = sect end
    local d_sect = sect - prev_sect
    prev_sect = sect

    -- Report each CD-audio mute as one window, with the sector rate inside it.
    local cd_on = (ctrl_now & 1) ~= 0
    if prev_ctrl == nil then prev_ctrl = cd_on end
    if cd_on ~= prev_ctrl then
        if not cd_on then
            mute_vb, mute_sect = vb, 0
        elseif mute_vb then
            local frames = vb - mute_vb
            emu.log(string.format(
                "CDMUTE vb=%d..%d  %.2fs  sectors_read=%d (%.1f/s, hardware streams 150/s)",
                mute_vb, vb - 1, frames * 0.02, mute_sect,
                frames > 0 and mute_sect / (frames * 0.02) or 0))
            mute_vb = nil
        end
        prev_ctrl = cd_on
    end
    if mute_vb then mute_sect = mute_sect + d_sect end

    -- vram_x/y: which VRAM pixel is the top-left of the picture (GP1(05)).
    -- hs/he:    horizontal display range in GPU clocks (GP1(06)).
    -- ls/le:    vertical display range in scanlines (GP1(07)).
    local vx, vy, hs, he, ls, le = emu.display_area()
    local key = string.format("%d,%d %d-%d %d-%d", vx, vy, hs, he, ls, le)
    if key == prev then return end
    local first = (prev == nil)
    prev = key

    local _, _, _, _, gen, ring_drop, und_ev, und_smp, ring = emu.audio_stats()
    local ctrl = select(1, emu.reverb())
    local tempo = emu.stretch()

    emu.log(string.format(
        "DISP vb=%-6d t=%7.2fs gap=%4dms | vram=(%4d,%3d) h=%4d-%4d (%3d clk) " ..
        "v=%3d-%3d (%3d lines) | ring=%5d und_ev=%d und_smp=%d drop=%d tempo=%.3f SPUCNT=0x%04X%s",
        vb, now / 1000.0, gap, vx, vy, hs, he, he - hs, ls, le, le - ls,
        ring, und_ev, und_smp, ring_drop, tempo or 1.0, ctrl,
        first and "  (initial)" or ""))
end)
