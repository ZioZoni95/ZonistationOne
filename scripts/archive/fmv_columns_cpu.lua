-- CPU-side half of the stripe hunt: is gpu.vram.data complete at the moment
-- the CRTC shows a buffer? Hash each of the 20 macroblock columns (24 halfwords
-- wide) of the *displayed* half at vblank, and report which columns changed
-- since the previous time that same half was displayed. Columns that are stale
-- on screen but changing here prove the loss is on the GL side, not in the
-- decode/DMA path.
local COLS, CW, ROWS = 20, 24, 160
local prev = {}          -- prev[y0][col] = hash
local vb, mbs = 0, 0

local function hash_col(x0, y0)
  local h = 0
  for row = 0, ROWS - 1, 8 do          -- every 8th row: cheap but distinctive
    for c = 0, CW - 1 do
      h = (h * 131 + (emu.vram16(x0 + c, y0 + row) or 0)) & 0xffffffff
    end
  end
  return h
end

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if mbs < 2000 or vb % 30 ~= 0 then return end
  local dx, dy = emu.display_area()
  local y0 = dy + 40                    -- movie sits 40 lines into the buffer
  prev[dy] = prev[dy] or {}
  local changed, same = {}, 0
  for col = 0, COLS - 1 do
    local h = hash_col(dx + col * CW, y0)
    if prev[dy][col] == h then same = same + 1 else changed[#changed + 1] = col end
    prev[dy][col] = h
  end
  emu.log(string.format("[CPUCOL vb=%d mb=%d] display=(%d,%d) unchanged=%d changed=%s",
                        vb, mbs, dx, dy, same, table.concat(changed, ",")))
end)
emu.log("[FMV COLUMNS CPU] armed")
