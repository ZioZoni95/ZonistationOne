-- Inspect a ZS1_AUDIO_DUMP capture and say what kind of defect is in it.
--
-- The dump is exactly what was handed to the sound device: interleaved signed
-- 16-bit stereo at 44100 Hz. A "pop" is one of three things in that stream, and
-- they need different fixes, so the point here is to tell them apart rather than
-- to confirm that something is wrong:
--
--   clipping      samples pinned at the rail. Sounds like distortion on loud
--                 material. Shows up as a run of values at +-32767.
--   discontinuity a step between adjacent samples far larger than the waveform's
--                 own slope. This is what a dropped or duplicated block sounds
--                 like, and it can happen at any level — including quiet ones,
--                 which is why "the volume is fine" does not rule it out.
--   dropout       a run of exact zeros in the middle of material.
--
-- Speech is smooth between samples: at 44.1 kHz even a bright consonant moves a
-- few hundred units per sample. A jump of thousands is not signal.
--
-- Run: ZS1_AUDIO_RAW=<path> ZS1_LUA_SCRIPT=scripts/audio_raw_analyse.lua ./myps1_emu ...
-- (analysis runs once at startup, before the machine does anything interesting)

local PATH       = os.getenv("ZS1_AUDIO_RAW") or "audio.raw"
local SKIP_SEC   = tonumber(os.getenv("ZS1_AUDIO_SKIP") or "0")
local MAX_SEC    = tonumber(os.getenv("ZS1_AUDIO_SECS") or "20")
local RATE       = 44100
local JUMP_LIMIT = 6000     -- adjacent-sample step treated as not-signal
local RAIL       = 32700    -- |sample| at or above this counts as pinned
local TOP_N      = 12       -- worst jumps to name individually

local f = io.open(PATH, "rb")
if not f then
  emu.log("[raw] cannot open " .. PATH)
  return
end

local size = f:seek("end")
f:seek("set", math.floor(SKIP_SEC * RATE) * 4)
local total_frames = math.floor(size / 4)

emu.log(string.format("[raw] %s — %d bytes, %d frames, %.1f s (skipping %.1f s)",
                      PATH, size, total_frames, total_frames / RATE, SKIP_SEC))

local peak_l, peak_r = 0, 0
local rail_l, rail_r = 0, 0
local zero_run, max_zero_run = 0, 0
local jumps = 0
local frames = 0
local prev_l, prev_r = nil, nil
local worst = {}          -- {delta, frame, channel}

local CHUNK = 1 << 16     -- bytes, multiple of 4
local unpack_i2 = string.unpack

while true do
  local buf = f:read(CHUNK)
  if not buf or #buf < 4 then break end
  local n = #buf - (#buf % 4)

  for i = 1, n, 4 do
    local l = unpack_i2("<i2", buf, i)
    local r = unpack_i2("<i2", buf, i + 2)
    frames = frames + 1

    local al = l < 0 and -l or l
    local ar = r < 0 and -r or r
    if al > peak_l then peak_l = al end
    if ar > peak_r then peak_r = ar end
    if al >= RAIL then rail_l = rail_l + 1 end
    if ar >= RAIL then rail_r = rail_r + 1 end

    if l == 0 and r == 0 then
      zero_run = zero_run + 1
      if zero_run > max_zero_run then max_zero_run = zero_run end
    else
      zero_run = 0
    end

    if prev_l then
      local dl = l - prev_l; if dl < 0 then dl = -dl end
      local dr = r - prev_r; if dr < 0 then dr = -dr end
      local d  = dl > dr and dl or dr
      if d >= JUMP_LIMIT then
        jumps = jumps + 1
        worst[#worst + 1] = { d = d, frame = frames, ch = (dl >= dr) and "L" or "R" }
      end
    end
    prev_l, prev_r = l, r
  end
end
f:close()

table.sort(worst, function(a, b) return a.d > b.d end)

emu.log(string.format("[raw] peak L=%d R=%d of 32767 (%.1f%% / %.1f%% of full scale)",
                      peak_l, peak_r, peak_l / 327.67, peak_r / 327.67))
emu.log(string.format("[raw] samples pinned at the rail: L=%d R=%d", rail_l, rail_r))
emu.log(string.format("[raw] longest run of digital silence: %d frames (%.0f ms)",
                      max_zero_run, max_zero_run * 1000 / RATE))
emu.log(string.format("[raw] adjacent-sample steps >= %d: %d over %d frames (%.1f s)",
                      JUMP_LIMIT, jumps, frames, frames / RATE))

if #worst > 0 then
  emu.log("[raw] worst steps, by size:")
  for i = 1, math.min(TOP_N, #worst) do
    local w = worst[i]
    emu.log(string.format("[raw]   %2d.  step %6d on %s at frame %8d  (t = %7.3f s)",
                          i, w.d, w.ch, w.frame, w.frame / RATE + SKIP_SEC))
  end
end

-- The reading, stated rather than left to inference.
if rail_l + rail_r > 0 then
  emu.log("[raw] verdict: samples reach the rail — there IS clipping at the output.")
elseif jumps > 0 then
  emu.log("[raw] verdict: no clipping (peak is below the rail), but the stream has " ..
          "steps too large to be signal. The pops are discontinuities, not distortion.")
else
  emu.log("[raw] verdict: no clipping and no discontinuities in this capture. " ..
          "Whatever is audible is not in the stream handed to the device.")
end
