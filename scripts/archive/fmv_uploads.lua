-- Are FMV column uploads being lost?
-- One 320x160 24bpp frame = 20 GP0(0xA0) uploads of 24 halfwords each. The
-- picture shows 16-pixel-wide stale column strips, and the DMA layer drops a
-- channel kick outright when a sliced transfer is still in flight. Count both.
local ups, drops, mbs, vb, frames = 0, 0, 0, 0, 0
local prev_remain, ups_frame = -1, 0
emu.on_event(function(name)
  if name == "gp0_vram_upload" then ups = ups + 1; ups_frame = ups_frame + 1; return end
  if name == "ch2_kick_dropped" then drops = drops + 1; return end
  if name == "mdec_macroblock" then
    mbs = mbs + 1
    local _, remain = emu.mdec_info()
    if remain > prev_remain then
      frames = frames + 1
      if frames > 2 and frames % 20 == 0 then
        print(string.format("[FRAME %d] uploads_prev_frame=%d total_ups=%d ch2_kicks_dropped=%d mbs=%d",
              frames, ups_frame, ups, drops, mbs))
      end
      ups_frame = 0
    end
    prev_remain = remain
  end
end)
print("[FMV UPLOADS] armed")
