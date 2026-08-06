-- Is the compressed video actually in RAM when the game decodes a frame?
-- A PSX BS/STR frame header is [u16 nCodes][u16 0x3800][u16 qscale][u16 version].
-- On the first FMV macroblock, scan all of RAM for that magic and dump the
-- candidates, so we can tell "no compressed data reached the decoder" from
-- "data is there, decoder/CPU mishandles it".

local done = false

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  done = true
  print(string.format("[SCAN] cyc=%d — scanning RAM for BS frame headers (0x3800 magic)", emu.cycles()))

  local found = 0
  local addr = 0x00010000
  while addr < 0x00200000 do
    local w = emu.read_u32(addr)
    if (w >> 16) == 0x3800 then
      found = found + 1
      if found <= 12 then
        local nc = w & 0xffff
        local w2 = emu.read_u32(addr + 4)
        local body = {}
        for i = 2, 9 do
          local x = emu.read_u32(addr + i * 4)
          body[#body + 1] = string.format("%08x", x)
        end
        print(string.format("  hdr @0x%08x nCodes=%d qscale=%d version=%d | %s",
              addr, nc, w2 & 0xffff, (w2 >> 16) & 0xffff, table.concat(body, " ")))
      end
    end
    addr = addr + 4
  end
  print(string.format("[SCAN] %d BS headers found in RAM", found))

  -- Also: how much of RAM is non-zero around the sector staging area?
  local sec = 0
  addr = 0x00010000
  while addr < 0x00200000 do
    if emu.read_u32(addr) ~= 0 then sec = sec + 1 end
    addr = addr + 4
  end
  print(string.format("[SCAN] non-zero words in RAM 0x10000-0x200000: %d", sec))
end)

print("[STR FRAME SCAN] armed")
