-- Dump, for ONE late FMV frame, both halves of the software decode:
--   (a) the compressed BS bitstream the game read from the CD, and
--   (b) the MDEC coefficient stream the game's VLC decoder produced from it.
-- With both in hand the decode can be replayed offline against the documented
-- BS/MDEC VLC table, which tells us whether the CPU is mis-executing the
-- decoder or the picture really is that dark.

local mb, done = 0, false

local function hexdump(tag, base, words)
  for i = 0, words - 1, 8 do
    local parts = {}
    for j = 0, 7 do parts[#parts + 1] = string.format("%08x", emu.read_u32(base + (i + j) * 4)) end
    print(string.format("%s %08x: %s", tag, base + i * 4, table.concat(parts, " ")))
  end
end

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  mb = mb + 1
  if mb < 4200 then return end
  done = true

  print(string.format("[PAIR] cyc=%d mb=%d", emu.cycles(), mb))

  -- (a) locate the BS frame ring buffers (headers at >= 0x183000, version 3,
  -- sane qscale — the 0x182cxx block is the sector-header ring, not payload).
  local slots = {}
  local addr = 0x00183000
  while addr < 0x00200000 do
    local w = emu.read_u32(addr)
    if (w >> 16) == 0x3800 then
      local w2 = emu.read_u32(addr + 4)
      local qs, ver = w2 & 0xffff, (w2 >> 16) & 0xffff
      if ver == 3 and qs > 0 and qs < 64 then
        slots[#slots + 1] = addr
        print(string.format("[BSHDR] @0x%08x nCodes=%d qscale=%d version=%d", addr, w & 0xffff, qs, ver))
      end
    end
    addr = addr + 4
  end
  for _, a in ipairs(slots) do
    print(string.format("[BS] slot 0x%08x nCodes=%d", a, emu.read_u32(a) & 0xffff))
    hexdump("[BS]", a, 640)
  end

  -- (b) the coefficient buffer the ch0 DMA is walking: rewind to the MDEC
  -- command word (top nibble 0x3) that heads it, then dump.
  local ia = emu.mdec_dma()
  local start = nil
  for k = 0, 0x3000, 4 do
    local w = emu.read_u32(ia - k)
    if (w >> 28) == 3 and (w & 0xffff) > 0x100 then start = ia - k; break end
  end
  if start then
    print(string.format("[COEF] cmd word 0x%08x at 0x%08x (ia=0x%08x)", emu.read_u32(start), start, ia))
    hexdump("[COEF]", start, 512)
  else
    print(string.format("[COEF] command word not found below ia=0x%08x", ia))
  end
end)

print("[FMV PAIR DUMP] armed")
