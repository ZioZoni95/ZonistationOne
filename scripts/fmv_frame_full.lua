-- Dump complete FMV frames (all 5 chunks, 10080 bytes) out of the game's ring
-- so each 2016-byte chunk can be matched against the disc sector it came from.
-- Chunk 0 was already proven byte-exact; the question is chunks 1..4.

local mb, done = 0, false

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  mb = mb + 1
  if mb < 4200 then return end
  done = true
  print(string.format("[FULL] cyc=%d", emu.cycles()))

  local slots = {}
  local addr = 0x00183000
  while addr < 0x00200000 do
    local w = emu.read_u32(addr)
    if (w >> 16) == 0x3800 then
      local w2 = emu.read_u32(addr + 4)
      local qs, ver = w2 & 0xffff, (w2 >> 16) & 0xffff
      if ver == 3 and qs > 0 and qs < 64 then slots[#slots + 1] = addr end
    end
    addr = addr + 4
  end

  for _, a in ipairs(slots) do
    print(string.format("[SLOT] 0x%08x nCodes=%d", a, emu.read_u32(a) & 0xffff))
    for i = 0, 2519, 8 do
      local parts = {}
      for j = 0, 7 do parts[#parts + 1] = string.format("%08x", emu.read_u32(a + (i + j) * 4)) end
      print(string.format("[D] %08x: %s", a + i * 4, table.concat(parts, " ")))
    end
  end
end)

print("[FULL FRAME DUMP] armed")
