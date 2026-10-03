-- Does the 8-word header DMA actually write all 8 words of the descriptor?
-- Disc says the STR header's last word (+28) is 0x00000000, but the descriptor
-- in RAM holds a stale value there. Watch the first, the +24 and the +28 word
-- of one ring entry and see which of them the transfer touches.

local armed, hits, mbs = false, 0, 0
local E = 0x80182d60
local watch = { E, E + 0x18, E + 0x1c, E + 0x20 }

emu.on_event(function(name)
  if name ~= "mdec_macroblock" then return end
  mbs = mbs + 1
  if armed or mbs < 6000 then return end
  armed = true
  for _, a in ipairs(watch) do emu.add_write_watch(a) end
  print(string.format("[DW] armed at mb %d cyc %d (entry 0x%08x)", mbs, emu.cycles(), E))
end)

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 24 then
    print(string.format("[DW %2d] %s cyc=%d pc=%08x | entry now: %08x %08x %08x %08x %08x %08x %08x %08x",
          hits, reason, emu.cycles(), emu.pc(),
          emu.read_u32(E), emu.read_u32(E + 4), emu.read_u32(E + 8), emu.read_u32(E + 12),
          emu.read_u32(E + 16), emu.read_u32(E + 20), emu.read_u32(E + 24), emu.read_u32(E + 28)))
  end
  if hits >= 24 then
    for _, a in ipairs(watch) do emu.remove_write_watch(a) end
    print("[DW] removed")
  end
  emu.resume()
end)

print("[DESC WORD WATCH] armed")
