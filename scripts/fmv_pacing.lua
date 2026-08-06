-- FMV pacing budget.
-- The picture is black because the movie never leaves its opening fade: frames
-- 18-22 after tens of emulated seconds, 46 MDEC commands per run. So the defect
-- is throughput, not colour. This measures where the per-frame cycle budget
-- goes: software VLC decode (CPU), MDEC macroblock decode, GPU upload, and the
-- leftover (waiting).
--
-- PAL frame = 680823 CPU cycles (gpu_cycles_per_frame), 15 fps movie => a frame
-- may spend ~2.26M cycles. Anything far above that is the bug.

local PAL_FRAME = 680823

local VLC_ENTRY = 0x80165a34
local VLC_EXIT1 = 0x80165d6c   -- jr ra, state saved (buffer full)
local VLC_EXIT2 = 0x80165d38   -- jr ra, error/abort path

emu.add_breakpoint(VLC_ENTRY)
emu.add_breakpoint(VLC_EXIT1)
emu.add_breakpoint(VLC_EXIT2)

local vlc_in, vlc_cyc, vlc_calls = nil, 0, 0

emu.on_break(function()
  local pc = emu.pc()
  if pc == VLC_ENTRY then
    vlc_in = emu.cycles()
  elseif vlc_in then
    vlc_cyc = vlc_cyc + (emu.cycles() - vlc_in)
    vlc_calls = vlc_calls + 1
    vlc_in = nil
  end
  emu.resume()
end)

local frames = 0
local prev_remain = -1
local f = nil          -- current frame accumulator
local last_frame_end = nil
local tot = { total = 0, mdec = 0, up = 0, vlc = 0, n = 0 }

local function flush()
  if not f or not f.mb_last then return end
  frames = frames + 1
  local total = (f.up_last or f.mb_last) - f.start
  if total <= 0 then return end
  local mdec = f.mb_last - f.mb_first
  local up   = (f.up_last and f.up_first) and (f.up_last - f.up_first) or 0
  local gap  = f.start - (last_frame_end or f.start)
  print(string.format(
    "[FRAME %2d] total=%8d cyc (%.2f PAL frames, %.1f fps) | vlc=%7d (%2d calls) mdec=%7d up=%7d(%d) | gap_before=%7d",
    frames, total, total / PAL_FRAME, 33868800.0 / total,
    f.vlc, f.vlc_calls, mdec, up, f.ups, gap))
  if frames > 3 then   -- skip warm-up frames in the average
    tot.total = tot.total + total; tot.mdec = tot.mdec + mdec
    tot.up = tot.up + up; tot.vlc = tot.vlc + f.vlc; tot.n = tot.n + 1
  end
  if tot.n == 12 then
    local n = tot.n
    print(string.format("[AVG over %d frames] total=%d cyc (%.1f fps) | vlc=%d (%.0f%%) mdec=%d (%.0f%%) up=%d (%.0f%%) other=%d (%.0f%%)",
      n, tot.total//n, 33868800.0 / (tot.total/n),
      tot.vlc//n,  100.0*tot.vlc/tot.total,
      tot.mdec//n, 100.0*tot.mdec/tot.total,
      tot.up//n,   100.0*tot.up/tot.total,
      (tot.total - tot.vlc - tot.mdec - tot.up)//n,
      100.0*(tot.total - tot.vlc - tot.mdec - tot.up)/tot.total))
  end
  last_frame_end = f.up_last or f.mb_last
end

emu.on_event(function(name)
  if name == "mdec_macroblock" then
    local _, remain = emu.mdec_info()
    if remain > prev_remain then          -- remain jumped up => new MDEC command
      flush()
      f = { start = emu.cycles(), mb_first = emu.cycles(), vlc = vlc_cyc, vlc_calls = vlc_calls, ups = 0 }
      f.vlc = 0; f.vlc_calls = 0
      vlc_cyc, vlc_calls = 0, 0
    end
    prev_remain = remain
    if f then f.mb_last = emu.cycles() end

  elseif name == "gp0_vram_upload" then
    if not f then return end
    if ((emu.gpustat() >> 21) & 1) ~= 1 then return end
    f.ups = f.ups + 1
    f.up_first = f.up_first or emu.cycles()
    f.up_last = emu.cycles()
    f.vlc = vlc_cyc          -- VLC work for the NEXT frame runs alongside
    f.vlc_calls = vlc_calls
  end
end)

print("[FMV PACING] armed")
