-- Columns uploaded to VRAM carry only two distinct payloads, alternating.
-- Is MDEC producing identical macroblocks, or is the transport (ch1 out-DMA ->
-- RAM -> ch2 upload) duplicating them? Hash MDEC's own decoded block_rgb for a
-- run of consecutive macroblocks.
local mbs, shown = 0, 0
local seq = {}
emu.on_event(function(name)
  if name ~= "mdec_macroblock" then return end
  mbs = mbs + 1
  if mbs < 22000 or shown >= 2 then return end
  local hash, peak = 0, 0
  for i = 0, 255 do
    local c = emu.mdec_block(i)
    hash = (hash * 31 + c) & 0xffffffff
    local r, g, b = c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff
    if r > peak then peak = r end
    if g > peak then peak = g end
    if b > peak then peak = b end
  end
  seq[#seq + 1] = string.format("mb %d hash=%08x peak=%02x", mbs, hash, peak)
  if #seq == 24 then
    shown = shown + 1
    print(string.format("--- 24 consecutive macroblocks (run %d) ---", shown))
    local seen = {}
    for i, l in ipairs(seq) do
      local h = l:match("hash=(%x+)")
      print(string.format("  [%2d] %s%s", i, l, seen[h] and (" == #" .. seen[h]) or ""))
      seen[h] = seen[h] or i
    end
    seq = {}
  end
end)
print("[MDEC BLOCK UNIQ] armed")
