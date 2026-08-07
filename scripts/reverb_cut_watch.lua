-- reverb_cut_watch.lua — everything that changes at the instant the audio cuts.
--
-- The cut is reported as always coinciding with the game switching reverb off.
-- SPUCNT bit 7 only stops writes to the reverb work area (DOCS:829), so the
-- unit keeps reading a buffer that no longer changes and keeps mixing its output
-- in. Whether that is what is heard depends on what else the game writes in the
-- same breath: bit 0 (CD audio enable) leaving the XA stream out of the mix is a
-- silence, a frozen buffer still fed through vLOUT/vROUT is a drone, and the two
-- have to be told apart before anything is changed.
--
-- Every line is one change, not one frame, so the whole run is a handful of
-- lines and the ordering inside the cut is readable.

local prev = nil

local function bits(c)
    local t = {}
    if c & 0x8000 ~= 0 then t[#t+1] = "SPUON"   end
    if c & 0x4000 == 0 then t[#t+1] = "MUTED"   end
    if c & 0x0080 ~= 0 then t[#t+1] = "REVERB"  end
    if c & 0x0004 ~= 0 then t[#t+1] = "CDREV"   end
    if c & 0x0001 ~= 0 then t[#t+1] = "CDAUDIO" end
    if c & 0x0002 ~= 0 then t[#t+1] = "EXT"     end
    local mode = (c >> 4) & 3
    t[#t+1] = string.format("xfer=%d", mode)
    return table.concat(t, "|")
end

local vb = 0

emu.on_event(function(name)
    if name ~= "vblank" then return end
    vb = vb + 1

    local ctrl, rev_en, vol_l, vol_r, eon, base, cur, in_l, in_r, out_l, out_r = emu.reverb()
    local _, _, _, _, gen, ring_drop, und_ev, und_smp, ring = emu.audio_stats()
    local _, _, _, _, _, _, sect = emu.cd_audio()

    local key = string.format("%04x %d %d %08x", ctrl, vol_l, vol_r, eon)
    if key == prev then return end
    prev = key

    emu.log(string.format(
        "SPUCNT vb=%-6d t=%7.2fs ctrl=0x%04X [%s] vLOUT=%6d vROUT=%6d EON=0x%06X " ..
        "rev_in=(%6d,%6d) rev_out=(%6d,%6d) ring=%5d und_ev=%d cd_sect=%d",
        vb, emu.host_ms() / 1000.0, ctrl, bits(ctrl), vol_l, vol_r, eon,
        in_l, in_r, out_l, out_r, ring, und_ev, sect))
end)
