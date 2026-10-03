-- Who fills the MDEC coefficient buffer, and with what store width?
-- Every word in that buffer reads back as 0xFE00<<16 | code, i.e. only the low
-- halfword of each 32-bit slot ever changes. Watch both halves of the first
-- slots (KSEG0 addresses — watchpoints match the virtual address) and report
-- the storing instruction.

local hits = 0
local watch = { 0x801bac04, 0x801bac06, 0x801bac08, 0x801bac0a, 0x801bac0c, 0x801bac0e }

for _, a in ipairs(watch) do emu.add_write_watch(a) end
print("[COEF-WRITER] watches armed")

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 12 then
    local pc = emu.pc()
    print(string.format("[HIT %d] %s pc=0x%08x ra=0x%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
    print(string.format("  v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x t0=%08x t1=%08x t2=%08x t9=%08x",
          emu.reg(2), emu.reg(3), emu.reg(4), emu.reg(5), emu.reg(6),
          emu.reg(8), emu.reg(9), emu.reg(10), emu.reg(25)))
    for i = -4, 4 do
      local a = pc + i * 4
      print(string.format("    %s %08x  %s", (i == 0) and "->" or "  ", a, emu.disasm(a)))
    end
  end
  if hits >= 12 then
    for _, a in ipairs(watch) do emu.remove_write_watch(a) end
    print("[COEF-WRITER] watches removed")
  end
  emu.resume()
end)
