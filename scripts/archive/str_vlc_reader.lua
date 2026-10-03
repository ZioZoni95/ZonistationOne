-- Find the game's software VLC decoder.
-- The compressed BS v3 bitstream sits at 0x00183000 (header 0x3800, nCodes=1216,
-- qscale=1) and real entropy-coded data follows it — so the data reaches RAM.
-- Whoever reads it is the decoder that is emitting blank blocks. Watchpoints
-- compare the VIRTUAL address, so watch KSEG0 (0x8018xxxx).

local hits = 0
local watch = { 0x80183100, 0x80183200, 0x80183400, 0x80183800,
                0x00183100, 0x00183200 }

for _, a in ipairs(watch) do emu.add_read_watch(a) end
print("[VLC-READER] read watches armed on the BS bitstream")

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 8 then
    local pc = emu.pc()
    print(string.format("[HIT %d] %s pc=0x%08x ra=0x%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
    print(string.format("  v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x t0=%08x t1=%08x",
          emu.reg(2), emu.reg(3), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7), emu.reg(8), emu.reg(9)))
    print(string.format("  s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x s5=%08x sp=%08x",
          emu.reg(16), emu.reg(17), emu.reg(18), emu.reg(19), emu.reg(20), emu.reg(21), emu.reg(29)))
    for i = -8, 8 do
      local a = pc + i * 4
      print(string.format("    %s %08x  %s", (i == 0) and "->" or "  ", a, emu.disasm(a)))
    end
  end
  if hits >= 8 then
    for _, a in ipairs(watch) do emu.remove_read_watch(a) end
    print("[VLC-READER] watches removed")
  end
  emu.resume()
end)
