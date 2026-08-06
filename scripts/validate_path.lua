-- Disassemble the CD ISR path that validates a descriptor (+28 = 1 at
-- 0x8016d1d4) and the branch that can skip it, plus the sector-accept code
-- around 0x8016d7c0-0x8016d840.

local mbs, done = 0, false

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  mbs = mbs + 1
  if mbs < 6000 then return end
  done = true
  print("[CODE] fn head 0x8016cf20..0x8016d060:")
  for a = 0x8016cf20, 0x8016d060, 4 do print(string.format("   %08x  %s", a, emu.disasm(a))) end
  
end)

print("[VALIDATE PATH] armed")
