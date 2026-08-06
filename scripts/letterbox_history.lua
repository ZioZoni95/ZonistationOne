-- The letterbox rows (x 512..991, the 40 lines above and below the 320x160
-- movie in each buffer) are never filled during playback. So either they were
-- already black before the movie and something dirties them, or they were never
-- black at all. Sample a few pixels of both halves every vblank and log only the
-- transitions, with the macroblock count as a clock.
local pts = {}
for _, y in ipairs({4, 20, 36, 204, 220, 236}) do
  for _, x in ipairs({520, 700, 900}) do pts[#pts + 1] = {x = x, y = y} end
end
local half = {[0] = {}, [240] = {}}
local vb, mbs, logged = 0, 0, 0

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  for _, base in ipairs({0, 240}) do
    local st = half[base]
    for i, p in ipairs(pts) do
      local v = emu.vram16(p.x, base + p.y) or 0
      if st[i] == nil then
        st[i] = v
      elseif st[i] ~= v and logged < 80 then
        logged = logged + 1
        emu.log(string.format("[LB CHANGE vb=%d mb=%d] (%d,%d) %04x -> %04x",
                              vb, mbs, p.x, base + p.y, st[i], v))
        st[i] = v
      elseif st[i] ~= v then
        st[i] = v
      end
    end
  end
end)
emu.log("[LETTERBOX HISTORY] armed")
