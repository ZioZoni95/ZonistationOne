-- Loads savestates/slot0.zst, then reports the audio delivery balance once a
-- second. The point is to separate two opposite failures that sound alike:
--
--   cd_drop rising  -> the XA producer outruns the 44100 Hz consumer, the CD
--                      FIFO sits at its latency cap and discards the oldest
--                      frame on every push. Discontinuities in a continuous
--                      stream: heard as buzz on speech, inaudible on effects.
--   spu_under rising -> the ring ran dry and the SDL callback padded silence.
--                      Same sound, opposite cause; drift would be negative.
--
-- The two cannot rise together. Whichever moves is the one to fix.

local loaded = false
local frames = 0
local prev = nil

local function snapshot()
  local push, pop, drop, queued,
        gen, ring_drop, under_ev, under_smp, ring = emu.audio_stats()
  return { push = push, pop = pop, drop = drop, queued = queued,
           gen = gen, ring_drop = ring_drop,
           under_ev = under_ev, under_smp = under_smp, ring = ring }
end

emu.on_event(function(name)
  if name ~= "vblank" then return end
  frames = frames + 1

  -- Load once, a few frames in, so the machine is fully up first.
  if not loaded and frames == 5 then
    loaded = true
    if emu.load_state() then
      emu.log("state loaded — probing audio delivery")
    else
      emu.log("no savestate found; probing from wherever we are")
    end
    prev = snapshot()
    return
  end
  if not loaded or frames % 50 ~= 0 then return end

  local now = snapshot()
  if prev then
    emu.log(string.format(
      "cd push %+6d  pop %+6d  DROP %+6d  q %5d | spu gen %+6d  ringdrop %+5d  UNDER %+4d ev/%+6d smp  ring %4d",
      now.push - prev.push, now.pop - prev.pop, now.drop - prev.drop, now.queued,
      now.gen - prev.gen, now.ring_drop - prev.ring_drop,
      now.under_ev - prev.under_ev, now.under_smp - prev.under_smp, now.ring))
  end
  prev = now
end)
