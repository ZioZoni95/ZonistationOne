-- Does the CRTC display origin actually flip during FMV playback?
-- The game double-buffers: it uploads columns into one half (y=40 or y=280)
-- while the other half is on screen, then flips with GP1(05). If our latched
-- origin never alternates we present the half being written - which looks
-- exactly like the 16px stale column strips on screen.
local vb, mbs, ups = 0, 0, 0
local hist = {}
emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name == "gp0_vram_upload" then
    local x, y = emu.vram_upload_rect()
    ups = ups + 1
    if mbs > 2000 and ups % 97 == 0 then
      local dx, dy = emu.display_area()
      print(string.format("[UP] rect=(%d,%d) | display=(%d,%d)", x, y, dx, dy))
    end
    return
  end
  if name ~= "vblank" then return end
  vb = vb + 1
  if mbs < 2000 then return end
  local dx, dy = emu.display_area()
  local k = string.format("(%d,%d)", dx, dy)
  hist[k] = (hist[k] or 0) + 1
  if vb % 300 == 0 then
    local parts = {}
    for kk, vv in pairs(hist) do parts[#parts+1] = kk .. "x" .. vv end
    print(string.format("[VB %d] display origins seen: %s", vb, table.concat(parts, " ")))
  end
end)
print("[DISP ORIGIN] armed")
