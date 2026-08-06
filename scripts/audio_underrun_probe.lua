-- audio_underrun_probe.lua — where the audio ring runs dry, and how far.
--
-- The SDL callback pulls 512 stereo samples at 44100 Hz from the ring the
-- emulator fills on the emulated SPU clock. When the ring is empty the
-- callback writes silence, which is heard as a click. This reports the ring
-- level and the underrun/drop deltas once a second, so a starved ring
-- (emulator behind) can be told apart from an overflowing one (emulator
-- ahead, samples discarded) — both end up sounding like dropouts.

local VB_PER_SEC = 50          -- PAL field rate
local vb = 0
local prev = { und_ev = 0, und_smp = 0, drop = 0, gen = 0, starve = 0, push = 0, pop = 0, sect = 0 }
local ring_min, ring_max = 1 << 30, 0
local worst = { sec = -1, smp = 0 }
-- Wallclock gap between vblanks. A PAL field is 20ms; anything far above that
-- is a stall, and a stall longer than the ring holds (~55ms at 2400 samples)
-- is exactly what makes the SDL callback find the ring empty.
local last_ms = nil
local gap_max, gap_sum, gap_n = 0, 0, 0
local last_disp = nil

emu.on_event(function(name)
    if name ~= "vblank" then return end
    vb = vb + 1

    local now = emu.host_ms()
    local gap = last_ms and (now - last_ms) or 0
    if last_ms then
        if gap > gap_max then gap_max = gap end
        gap_sum = gap_sum + gap; gap_n = gap_n + 1
    end
    last_ms = now

    -- The display window moving is what "the picture re-centres" looks like from
    -- here. Reported with the gap of the same vblank, so a re-centre that costs
    -- a stall is visible as one line instead of two correlated ones.
    -- vram_y alternates every frame between the two buffers; that is the page
    -- flip, not a geometry change, so it stays out of the signature.
    local vx, vy, hs, he, ls, le = emu.display_area()
    local sig = string.format("%d %d-%d %d-%d", vx, hs, he, ls, le)
    if sig ~= last_disp then
        emu.log(string.format("[AUD] vb=%d display -> vram(%d,%d) horiz %d-%d line %d-%d  gap=%dms",
            vb, vx, vy, hs, he, ls, le, gap))
        last_disp = sig
    end

    local _, _, _, _, gen, drop, und_ev, und_smp, ring = emu.audio_stats()
    if ring < ring_min then ring_min = ring end
    if ring > ring_max then ring_max = ring end

    if vb % VB_PER_SEC ~= 0 then return end

    local sec = vb // VB_PER_SEC
    local d_ev  = und_ev  - prev.und_ev
    local d_smp = und_smp - prev.und_smp
    local d_drop = drop - prev.drop
    local d_gen = gen - prev.gen
    prev.und_ev, prev.und_smp, prev.drop, prev.gen = und_ev, und_smp, drop, gen

    if d_smp > worst.smp then worst.sec, worst.smp = sec, d_smp end

    -- Silence has two different causes that sound identical: the ring running
    -- dry (underrun, counted above) and the mix itself producing zeros because
    -- the SPU is muted, main volume is zero, or the XA FIFO is starving. This
    -- half of the line names which one it is.
    -- starve counts every sample the mixer wanted CD audio and found the FIFO
    -- empty. On its own that cannot tell "no XA stream is playing" from "the XA
    -- stream is playing and we are losing it" — pushed/popped is what separates
    -- them, so both are reported as per-second deltas.
    local xa_n, xa_push, xa_pop, xa_drop, xa_starve, ctrl, _, xa_sect = emu.cd_audio()
    local d_starve = xa_starve - prev.starve
    local d_push   = xa_push - prev.push
    local d_pop    = xa_pop - prev.pop
    local d_sect   = xa_sect - prev.sect
    prev.starve, prev.push, prev.pop, prev.sect = xa_starve, xa_push, xa_pop, xa_sect
    local enable = (ctrl & 0x8000) ~= 0
    local unmuted = (ctrl & 0x4000) ~= 0
    local cd_on = (ctrl & 0x0001) ~= 0

    emu.log(string.format(
        "[AUD] s=%3d gen=%5d (%+5d) underrun %2d ev / %5d smp (%.1f%% muto) " ..
        "drop=%5d ring=%5d [min %d max %d] gap avg=%.1fms max=%.1fms | " ..
        "SPUCNT=0x%04X en=%s unmuted=%s cdaudio=%s | XA fifo=%d push+%d pop+%d starve+%d drop=%d sect+%d",
        sec, d_gen, d_gen - 44100, d_ev, d_smp, d_smp * 100.0 / 44100.0,
        d_drop, ring, ring_min, ring_max,
        gap_n > 0 and gap_sum / gap_n or 0, gap_max,
        ctrl, tostring(enable), tostring(unmuted), tostring(cd_on),
        xa_n, d_push, d_pop, d_starve, xa_drop, d_sect))
    ring_min, ring_max = 1 << 30, 0
    gap_max, gap_sum, gap_n = 0, 0, 0

    if sec % 30 == 0 then
        emu.log(string.format("[AUD] === totali a s=%d: underrun %d ev / %d smp, drop %d, peggior secondo s=%d (%d smp)",
            sec, und_ev, und_smp, drop, worst.sec, worst.smp))
    end
end)
