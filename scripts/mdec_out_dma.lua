-- MDEC decodes 24 distinct macroblocks, yet the columns uploaded to VRAM carry
-- only two alternating payloads. So the duplication is in the MDEC-output
-- transport. Log every ch1 (MDEC out) completion: destination pointer and a
-- hash of the words just written.
local mbs, n, shown = 0, 0, 0
emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "mdec_ch1_done" then return end
  n = n + 1
  if mbs < 22000 or shown >= 16 then return end
  shown = shown + 1
  local _, _, out_addr, out_rem = emu.mdec_dma()
  local base = (out_addr - 0x300) & 0xfffffffc
  local hash = 0
  for i = 0, 191 do hash = (hash * 31 + emu.read_u32(base + i * 4)) & 0xffffffff end
  print(string.format("[ch1 #%d mb=%d] out_addr=0x%08x rem=%d | last192w from 0x%08x hash=%08x first=%08x %08x",
        n, mbs, out_addr, out_rem, base, hash, emu.read_u32(base), emu.read_u32(base + 4)))
end)
print("[MDEC OUT DMA] armed")
