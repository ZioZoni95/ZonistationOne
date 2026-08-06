-- VRAM (CPU model) holds a clean FMV frame while the GL texture shows grey
-- bands. The GL texture is also the rasteriser's render target, so anything the
-- game draws lands there but NOT in the CPU model - that asymmetry fits exactly.
-- Count primitives issued while the movie is playing.
local mbs, poly, rect, frames = 0, 0, 0, 0
local prev_remain, p0, r0 = -1, 0, 0
emu.on_event(function(name)
  if name == "gp0_poly" then poly = poly + 1; return end
  if name == "gp0_rect" then rect = rect + 1; return end
  if name ~= "mdec_macroblock" then return end
  mbs = mbs + 1
  local _, remain = emu.mdec_info()
  if remain > prev_remain then
    frames = frames + 1
    if frames > 4 and frames % 25 == 0 then
      print(string.format("[FMV frame %d] polys=%d (+%d) rects=%d (+%d) since last report",
            frames, poly, poly - p0, rect, rect - r0))
      p0, r0 = poly, rect
    end
  end
  prev_remain = remain
end)
print("[FMV DRAWS] armed")
