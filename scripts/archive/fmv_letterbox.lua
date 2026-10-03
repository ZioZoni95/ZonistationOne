-- The FMV is 320x160 uploaded 40 lines into a 320x240 CRTC window, so the
-- 40 rows above and 40 below it are letterbox. On screen they show stale VRAM;
-- this checks what the CPU-side VRAM model holds there while the movie plays.
-- Black on the CPU side + garbage on screen = the clear never reaches the
-- unified GL texture (same class of bug as the is_viewer one).
local vb, mbs = 0, 0

local function scan(x0, y0, rows)
  local nz, total, sample = 0, 0, {}
  for row = 0, rows - 1, 4 do
    for c = 0, 479, 16 do
      local v = emu.vram16(x0 + c, y0 + row) or 0
      if v ~= 0 then nz = nz + 1 end
      total = total + 1
    end
  end
  for i = 0, 3 do sample[#sample + 1] = string.format("%04x", emu.vram16(x0 + i * 64, y0 + 8) or 0) end
  return nz, total, table.concat(sample, " ")
end

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if mbs < 2000 or vb % 60 ~= 0 then return end
  local dx, dy = emu.display_area()
  local tn, tt, ts = scan(dx, dy, 40)               -- rows above the movie
  local bn, bt, bs = scan(dx, dy + 200, 40)         -- rows below the movie
  local mn, mt, ms = scan(dx, dy + 40, 8)           -- first rows of the movie
  emu.log(string.format(
    "[LETTERBOX vb=%d mb=%d] disp=(%d,%d) top nonzero=%d/%d [%s] | bottom nonzero=%d/%d [%s] | movie nonzero=%d/%d [%s]",
    vb, mbs, dx, dy, tn, tt, ts, bn, bt, bs, mn, mt, ms))
end)
emu.log("[FMV LETTERBOX] armed")
