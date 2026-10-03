-- One FMV frame = 20 GP0(0xA0) uploads, each a 16px-wide macroblock column
-- (24 halfwords x 160). The picture shows the same column repeated, so either
-- two uploads carry identical pixels (MDEC output / ch1 DMA reusing data) or
-- they land at the wrong X. Log rect + a cheap hash of what actually sits in
-- VRAM for each upload of one frame.
local mbs, ups, printed = 0, 0, 0
local frame = {}
emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "gp0_vram_upload" then return end
  if mbs < 6000 or printed >= 2 then return end
  local x, y, w, h = emu.vram_upload_rect()
  local hash, peak = 0, 0
  for row = 0, h - 1 do
    for col = 0, w - 1 do
      local v = emu.vram16(x + col, y + row) or 0
      hash = (hash * 31 + v) & 0xffffffff
      local lo, hi = v & 0xff, (v >> 8) & 0xff
      if lo > peak then peak = lo end
      if hi > peak then peak = hi end
    end
  end
  local px = {}
  for i = 0, 3 do px[#px+1] = string.format("%04x", emu.vram16(x + i, y + 80) or 0) end
  frame[#frame + 1] = string.format("x=%3d y=%3d %dx%d hash=%08x peak=%02x mid=%s", x, y, w, h, hash, peak, table.concat(px, " "))
  ups = ups + 1
  if #frame == 20 then
    local bright = false
    for _, l in ipairs(frame) do
      local pk = tonumber(l:match("peak=(%x+)"), 16) or 0
      if pk > 0x40 then bright = true end
    end
    if not bright then frame = {}; return end
    printed = printed + 1
    print(string.format("--- frame %d uploads (mb %d) ---", printed, mbs))
    local seen = {}
    for i, l in ipairs(frame) do
      local hsh = l:match("hash=(%x+)")
      local dup = seen[hsh] and (" DUPLICATE of #" .. seen[hsh]) or ""
      seen[hsh] = seen[hsh] or i
      print(string.format("  [%2d] %s%s", i, l, dup))
    end
    frame = {}
  end
end)
print("[UPLOAD COLUMNS] armed")
