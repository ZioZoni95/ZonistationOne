-- The guest sits at ~0x800678c8 whenever a vblank fires and the BIOS keeps
-- reporting "VSync: timeout". Dump that loop once so we can read what it polls.
local done = false
emu.on_event(function(name)
  if name ~= "vblank" or done then return end
  local pc = emu.pc()
  if pc < 0x80067800 or pc > 0x80067a00 then return end
  done = true
  emu.log(string.format("[VSYNC DISASM] pc=%08x — dumping the surrounding loop", pc))
  for a = pc - 0x60, pc + 0x40, 4 do
    local mark = (a == pc) and "  <-- pc" or ""
    emu.log(string.format("  %08x  %s%s", a, emu.disasm(a), mark))
  end
end)
emu.log("[VSYNC DISASM] armed")
