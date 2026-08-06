-- Catch the exact transition where the game stops accepting movie chunks.
-- Per INT1 record what the game's sector-descriptor ring (base 0x80182c00,
-- 32-byte entries, consumer index at 0x801ECFDC) looks like, and dump the whole
-- ring the moment MDEC falls silent (8 VBlanks of no macroblocks).

local RING_BASE = 0x80182c00
local IDX_PTR   = 0x801ECFDC
local BASE_PTR  = 0x801ECFF0

local hist, n, mbs, vb, last_mb_vb, dumped = {}, 0, 0, 0, 0, false

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end

  if name == "cdrom_int1" then
    n = n + 1
    local idx  = emu.read_u32(IDX_PTR)
    local base = emu.read_u32(BASE_PTR)
    local e = (base >= 0x80000000 and base < 0x80200000) and base + (idx % 32) * 32 or RING_BASE
    hist[#hist + 1] = string.format("int1 #%4d vb=%4d cyc=%d idx=%3d e[%08x]=%08x %08x %08x %08x",
          n, vb, emu.cycles(), idx, e,
          emu.read_u32(e), emu.read_u32(e + 4), emu.read_u32(e + 8), emu.read_u32(e + 12))
    if #hist > 120 then table.remove(hist, 1) end
    return
  end

  if name ~= "vblank" then return end
  vb = vb + 1
  if mbs > 0 and (vb - last_mb_vb) == 8 and not dumped then
    dumped = true
    print(string.format("[T] MDEC stopped: vb=%d cyc=%d int1=%d mbs=%d", vb, emu.cycles(), n, mbs))
    print(string.format("[T] idx=%d base=0x%08x", emu.read_u32(IDX_PTR), emu.read_u32(BASE_PTR)))
    print("[T] descriptor ring (chunk/nchunks/frame per entry):")
    for i = 0, 31 do
      local e = RING_BASE + i * 32
      local w0, w1, w2, w3 = emu.read_u32(e), emu.read_u32(e + 4), emu.read_u32(e + 8), emu.read_u32(e + 12)
      print(string.format("   [%2d] %08x: %08x %08x %08x %08x  magic=%04x chunk=%d/%d frame=%d",
            i, e, w0, w1, w2, w3, w1 & 0xffff, w2 & 0xffff, (w2 >> 16) & 0xffff, w3))
    end
    print("[T] last INT1s before the stop:")
    for i = math.max(1, #hist - 40), #hist do print("   " .. hist[i]) end
  end
end)

print("[FMV ABORT 2] armed")
