-- Who sets the descriptor status, and why does it stop reaching 1?
-- Descriptor ring at 0x80182c00, 32 bytes/entry: [u16 status][u16 0x8001]
-- [u16 chunk][u16 nchunks][u32 frame][u32 frameBytes]. The movie decoder waits
-- for status==1; after frame ~47 entries stay at 3 forever. Watch the status
-- word of a few entries (armed late, so boot-time clears don't eat the budget)
-- and report the writing instruction.

local armed, hits, mbs = false, 0, 0
local watch = { 0x80182e00, 0x80182ea0, 0x80182c00, 0x80182d60 }

emu.on_event(function(name)
  if name ~= "mdec_macroblock" then return end
  mbs = mbs + 1
  if armed or mbs < 7000 then return end
  armed = true
  for _, a in ipairs(watch) do emu.add_write_watch(a) end
  print(string.format("[DESC] watches armed at mb %d cyc %d", mbs, emu.cycles()))
  print("[DESC] consumer loop:")
  for a = 0x8016ce30, 0x8016cf00, 4 do
    print(string.format("   %08x  %s", a, emu.disasm(a)))
  end
end)

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 10 then
    local pc = emu.pc()
    print(string.format("[HIT %d] %s pc=%08x ra=%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
    print(string.format("   v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x t0=%08x s0=%08x s1=%08x",
          emu.reg(2), emu.reg(3), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7),
          emu.reg(8), emu.reg(16), emu.reg(17)))
    for i = -6, 4 do
      print(string.format("     %s %08x  %s", (i == 0) and "->" or "  ", pc + i * 4, emu.disasm(pc + i * 4)))
    end
  end
  if hits >= 10 then
    for _, a in ipairs(watch) do emu.remove_write_watch(a) end
    print("[DESC] watches removed")
  end
  emu.resume()
end)

print("[DESC STATUS WATCH] armed")
