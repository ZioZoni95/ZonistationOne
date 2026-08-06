-- Dump the game's software VLC decoder so its control flow can be read.
-- Entry region established from watchpoint hits: bit refill at 0x80165b60 /
-- 0x80165c78, DC store at 0x80165bb4, caller ra = 0x80161a18.

local done = false

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  done = true
  print(string.format("[VLC-DISASM] cyc=%d", emu.cycles()))
  local a = 0x80165980
  while a < 0x80165e00 do
    print(string.format("  %08x  %s", a, emu.disasm(a)))
    a = a + 4
  end
  print("[VLC-DISASM] caller context")
  a = 0x801619c0
  while a < 0x80161a40 do
    print(string.format("  %08x  %s", a, emu.disasm(a)))
    a = a + 4
  end
end)

print("[VLC DISASM] armed")
