-- Same question as coef_writer_watch, but armed only once the FMV is running,
-- so boot-time memory clears don't eat the hit budget. The coefficient buffer
-- (0x801bac00) is reused frame after frame, so arming during frame N catches
-- the software VLC decoder filling frame N+1.

local armed, hits = false, 0
local watch = {}

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or armed then return end
  armed = true
  for i = 0, 7 do watch[#watch + 1] = 0x801bac04 + i * 4 end
  for _, a in ipairs(watch) do emu.add_write_watch(a) end
  print(string.format("[COEF-WRITER2] armed at cyc %d", emu.cycles()))
end)

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 10 then
    local pc = emu.pc()
    print(string.format("[HIT %d] %s pc=0x%08x ra=0x%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
    print(string.format("  v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x t0=%08x t1=%08x t2=%08x t9=%08x",
          emu.reg(2), emu.reg(3), emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7),
          emu.reg(8), emu.reg(9), emu.reg(10), emu.reg(25)))
    for i = -5, 5 do
      local a = pc + i * 4
      print(string.format("    %s %08x  %s", (i == 0) and "->" or "  ", a, emu.disasm(a)))
    end
  end
  if hits >= 10 then
    for _, a in ipairs(watch) do emu.remove_write_watch(a) end
    print("[COEF-WRITER2] watches removed")
  end
  emu.resume()
end)

print("[COEF-WRITER2] waiting for FMV")
