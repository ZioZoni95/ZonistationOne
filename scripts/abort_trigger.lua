-- The global at 0x801EB6F0 flips from 1 to 0x8016198c exactly when the movie
-- dies, so the game aborts on purpose. Catch the write and disassemble the
-- decision that leads to it.

local armed, hits, mbs = false, 0, 0
local W = 0x801EB6F0

emu.on_event(function(name)
  if name ~= "mdec_macroblock" then return end
  mbs = mbs + 1
  if armed or mbs < 8000 then return end
  armed = true
  emu.add_write_watch(W)
  print(string.format("[TRIG] watch armed at mb %d cyc %d", mbs, emu.cycles()))
end)

emu.on_break(function(reason)
  local pc = emu.pc()
  -- the normal per-sector copy stores v0==1; only report anything else
  if (pc == 0x8016cc00 or pc == 0x8016cc04) and emu.reg(2) == 1 then emu.resume(); return end
  hits = hits + 1
  print(string.format("[HIT %d] %s pc=%08x ra=%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
  print(string.format("   v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x t0=%08x t1=%08x s0=%08x s1=%08x s2=%08x",
        emu.reg(2), emu.reg(3), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7),
        emu.reg(8), emu.reg(9), emu.reg(16), emu.reg(17), emu.reg(18)))
  local d = emu.reg(3)   -- v1 = descriptor base
  local w = {}
  for i = 0, 7 do w[#w+1] = string.format("%08x", emu.read_u32(d + i*4)) end
  print(string.format("   descriptor 0x%08x: %s", d, table.concat(w, " ")))
  print(string.format("   -> status=%04x magic=%04x chunk=%d/%d frame=%d bytes=%d w/h=%08x nCodes=%04x",
        emu.read_u32(d) & 0xffff, (emu.read_u32(d) >> 16) & 0xffff,
        emu.read_u32(d+4) & 0xffff, (emu.read_u32(d+4) >> 16) & 0xffff,
        emu.read_u32(d+8), emu.read_u32(d+12), emu.read_u32(d+16),
        emu.read_u32(d+20) & 0xffff))
  for i = -14, 6 do
    print(string.format("     %s %08x  %s", (i == 0) and "->" or "  ", pc + i * 4, emu.disasm(pc + i * 4)))
  end
  if hits >= 4 then emu.remove_write_watch(W); print("[TRIG] watch removed") end
  emu.resume()
end)

print("[ABORT TRIGGER] armed")
