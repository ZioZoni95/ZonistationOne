-- Who is supposed to blank the FMV letterbox, and does that write have any
-- chance of reaching the unified GL texture?  GP0(02) fills go into the CPU
-- VRAM unclipped (correct: fill ignores drawing area and offset) but reach the
-- texture only through the GL raster path, which scissors every batch to the
-- drawing area and adds the drawing offset. Log each fill during the FMV with
-- the drawing area/offset in force, and flag the ones that would be clipped.
local mbs, fills, shown = 0, 0, 0

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "gp0_fill" then return end
  fills = fills + 1
  if shown >= 60 then return end
  local pos, dim = emu.gp0_word(1), emu.gp0_word(2)
  local x = pos & 0x3F0
  local y = (pos >> 16) & 0x1FF
  local w = ((dim & 0x3FF) + 0xF) & ~0xF
  local h = (dim >> 16) & 0x1FF
  local l, t, r, b, ox, oy = emu.draw_area()
  local dx, dy = emu.display_area()
  -- draw_rectangle pre-subtracts the drawing offset and the vertex shader adds
  -- it back, so the scissor test is against the raw VRAM rect.
  local clip = ""
  if (x + w <= l) or (x > r) or (y + h <= t) or (y > b) then
    clip = "FULLY CLIPPED from the GL path"
  elseif x < l or y < t or (x + w - 1) > r or (y + h - 1) > b then
    clip = "PARTIALLY CLIPPED from the GL path"
  else
    clip = "reaches the texture"
  end
  shown = shown + 1
  emu.log(string.format(
    "[FILL %d] rect=(%d,%d) %dx%d colour=%06x | draw_area=(%d,%d)-(%d,%d) off=(%d,%d)"
    .. " | display=(%d,%d) mb=%d | %s",
    fills, x, y, w, h, emu.gp0_word(0) & 0xffffff, l, t, r, b, ox, oy, dx, dy, mbs, clip))
end)
emu.log("[FMV FILL WATCH] armed")
