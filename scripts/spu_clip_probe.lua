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
-- Loads savestates/slot0.zst a few frames in, so every run starts inside the
-- scene where the pops are heard instead of booting to it. Set STATE to nil to
-- probe from a cold boot instead.
--
-- Run:  ZS1_LUA_SCRIPT=scripts/spu_clip_probe.lua ./myps1_emu <bios> --game=<bin>
-- A/B:  add ZS1_SPU_NO_REVERB=1 to take the reverb network out of the path.

local STATE = "savestates/slot0.zst"

-- Load well after boot, not a handful of vblanks in.
--
-- Restoring at vblank 5 wedges the emulator: the window stops responding and the
-- process has to be killed. Loading the same state by hand once the machine is up
-- works every time, so the state is fine and the restore path is fine — what is
-- not safe is doing it while boot is still in flight. Tracked as a real defect;
-- until it is fixed, wait. 300 vblanks is six seconds at PAL, comfortably past
-- the BIOS handover.
local LOAD_AT = 300

local NEAR_RAIL = 30000        -- |sample| above this counts as riding the limit
local REPORT_EVERY = 50        -- vblanks; PAL field rate, so about one second

local frames = 0
local prev = nil
local prev_starve = nil
local loaded = false
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

  if STATE and not loaded and frames == LOAD_AT then
    loaded = true
    emu.log("[clip] loading " .. STATE)
    emu.load_state(STATE)
    return          -- the load lands in the host loop; start measuring after it
  end
  if STATE and frames < LOAD_AT + 2 then return end

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
  local cd_count, cd_push, cd_pop, cd_drop, cd_starve = emu.cd_audio()
  local _, _, _, _, _, ring_drop, under_ev = emu.audio_stats()
  local gen, _, _, _, keyons = emu.spu_stats()

  -- Which source is actually feeding the mixer. XA streaming shows as cd_pop
  -- climbing by roughly the sample rate each second; SPU voices show as key-on
  -- events and nothing on the CD side. That fork decides where a defect can
  -- possibly come from, so it is worth more than any single level reading.
  prev = prev or { pop = cd_pop, keyons = keyons, gen = gen }
  local d_pop, d_keys = cd_pop - prev.pop, keyons - prev.keyons
  prev = { pop = cd_pop, keyons = keyons, gen = gen }

  emu.log(string.format(
    "[clip] rvb %s | xa: fifo=%4d +%6d/s drop=%d STARVED=%d (+%d/s) | keyon +%d | ring_drop=%d under=%d",
    win.reverb_on_frames > 0 and "ON " or "off",
    cd_count, d_pop, cd_drop, cd_starve, cd_starve - (prev_starve or cd_starve),
    d_keys, ring_drop, under_ev))
  prev_starve = cd_starve

  reset()
end)
