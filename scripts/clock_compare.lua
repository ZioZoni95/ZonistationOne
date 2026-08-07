-- clock_compare.lua — is the emulated machine running at the right speed?
--
-- Three clocks have to agree for the machine to be in time with itself:
--   the CPU clock   33 868 800 cycles per second of wallclock
--   the video clock 50 fields per second (PAL)
--   the audio clock 44 100 samples per second
-- Each is reported per wallclock second as a ratio against its nominal rate,
-- so "the emulator runs fast" (all three above 1.00), "video is ahead of audio"
-- (fields high, samples at 1.00) and "the host cannot keep up" (all three below
-- 1.00) are three different lines rather than one feeling.
--
-- The ring level and underrun deltas are on the same line because a ratio
-- mismatch only matters if it is actually costing samples.

local NOMINAL_HZ    = 33868800.0
local NOMINAL_FIELD = 50.0        -- PAL
local NOMINAL_SMP   = 44100.0

local start_ms   = nil
local mark_ms    = nil
local mark_cyc   = 0
local mark_smp   = 0
local mark_vb    = 0
local mark_und   = 0
local mark_drop  = 0
local mark_sect  = 0
local vb         = 0

emu.on_event(function(name)
    if name ~= "vblank" then return end
    vb = vb + 1

    local now = emu.host_ms()
    if not start_ms then
        start_ms, mark_ms = now, now
        mark_cyc = emu.cycles()
        local _, _, _, _, gen, drop, und = emu.audio_stats()
        mark_smp, mark_drop, mark_und = gen, drop, und
        return
    end

    local dt = now - mark_ms
    if dt < 1000 then return end

    local cyc = emu.cycles()
    local cd_push, cd_pop, cd_drop, cd_q, gen, ring_drop, und_ev, und_smp, ring = emu.audio_stats()
    local _, _, _, _, _, _, sect = emu.cd_audio()

    local secs   = dt / 1000.0
    local r_cpu  = ((cyc - mark_cyc) / secs) / NOMINAL_HZ
    local r_vid  = ((vb  - mark_vb)  / secs) / NOMINAL_FIELD
    local r_aud  = ((gen - mark_smp) / secs) / NOMINAL_SMP

    local tempo, st_active, st_periods, st_blocks, st_queued = emu.stretch()

    emu.log(string.format(
        "CLK t=%5.1fs cpu=%.4f vid=%.4f aud=%.4f | ring=%5d und_ev=%d und_smp=%d ringdrop=%d | " ..
        "tempo=%.4f%s stq=%5d stblk=%d | cd_sect=%d cd_q=%d",
        (now - start_ms) / 1000.0, r_cpu, r_vid, r_aud,
        ring, und_ev - mark_und, und_smp, ring_drop - mark_drop,
        tempo or 1.0, st_active and "*" or " ", st_queued or 0, st_blocks or 0,
        sect - mark_sect, cd_q))

    mark_ms, mark_cyc, mark_smp, mark_vb = now, cyc, gen, vb
    mark_und, mark_drop, mark_sect = und_ev, ring_drop, sect
end)
