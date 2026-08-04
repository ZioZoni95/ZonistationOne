-- Where the speech "pop" saturates.
--
-- The symptom sounds like clipping, but the final mix peaks nowhere near full
-- scale (5869/6343 of 32767 observed in the Audio panel), so the clip is not at
-- the output. This samples the stages that sit before it, once per vblank, and
-- reports which of them is riding the rail.
--
--   reverb in   — what the mixer feeds the reverb network
--   reverb out  — what comes back, after the 22050 Hz network and the FIR
--                 upsample. Overshoot in a FIR is the classic way a transient
--                 saturates a stage whose steady-state level is modest.
--
-- Speech is transient-heavy, so a stage that only clips on attacks reads as a
-- low average with occasional rail hits — which is exactly the shape to look for
-- here, not a high mean.
--
-- Run:  ZS1_LUA_SCRIPT=scripts/spu_clip_probe.lua ./myps1_emu <bios> --game=<bin>
-- A/B:  add ZS1_SPU_NO_REVERB=1 to take the reverb network out of the path.

local NEAR_RAIL = 30000        -- |sample| above this counts as riding the limit
local REPORT_EVERY = 50        -- vblanks; PAL field rate, so about one second

local frames = 0
local win = {
  in_peak = 0, out_peak = 0,
  in_rail = 0, out_rail = 0,
  samples = 0, reverb_on_frames = 0,
}

local function absmax(a, b)
  a = a < 0 and -a or a
  b = b < 0 and -b or b
  return a > b and a or b
end

local function reset()
  win.in_peak, win.out_peak = 0, 0
  win.in_rail, win.out_rail = 0, 0
  win.samples, win.reverb_on_frames = 0, 0
end

emu.on_event(function(name)
  if name ~= "vblank" then return end
  frames = frames + 1

  local control, enabled, vol_l, vol_r, eon, base, cur,
        in_l, in_r, out_l, out_r = emu.reverb()

  local ip = absmax(in_l,  in_r)
  local op = absmax(out_l, out_r)

  if ip > win.in_peak  then win.in_peak  = ip end
  if op > win.out_peak then win.out_peak = op end
  if ip >= NEAR_RAIL   then win.in_rail  = win.in_rail  + 1 end
  if op >= NEAR_RAIL   then win.out_rail = win.out_rail + 1 end
  if enabled           then win.reverb_on_frames = win.reverb_on_frames + 1 end
  win.samples = win.samples + 1

  if frames % REPORT_EVERY ~= 0 then return end

  -- Delivery counters too: a pop from a dropped or missing sample is a different
  -- defect from a pop from saturation, and the two sound alike.
  local cd_count, cd_push, cd_pop, cd_drop = emu.cd_audio()
  local _, _, _, _, _, ring_drop, under_ev = emu.audio_stats()

  emu.log(string.format(
    "[clip] rvb %s eon=%04x vol=%d/%d | in peak=%6d rail=%2d/%d | out peak=%6d rail=%2d/%d | xa_drop=%d ring_drop=%d under=%d",
    win.reverb_on_frames > 0 and "ON " or "off", eon, vol_l, vol_r,
    win.in_peak,  win.in_rail,  win.samples,
    win.out_peak, win.out_rail, win.samples,
    cd_drop, ring_drop, under_ev))

  reset()
end)
