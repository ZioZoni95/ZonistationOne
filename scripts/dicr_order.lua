-- Exact ordering of DICR / CHCR writes around the CD payload DMA.
-- Our MANUAL DMA completes synchronously inside the CHCR write, so if the game
-- enables the channel-3 completion IRQ in DICR *after* kicking the transfer
-- (legal on hardware, where the transfer takes ~2000 cycles), we sample DICR
-- too early and never raise IRQ3. 46 completions for 247 starts fits that.

local BPs = { 0x8016d0b4, 0x8016d1f8, 0x8016d830, 0x8016cbe8 }
local names = { [0x8016d0b4] = "REGW-A", [0x8016d1f8] = "REGW-B",
                [0x8016d830] = "START ", [0x8016cbe8] = "DONE  " }
for _, a in ipairs(BPs) do emu.add_breakpoint(a) end

local trace, vb, mbs, last_mb_vb, dumped = {}, 0, 0, 0, false

emu.on_break(function()
  local pc = emu.pc()
  local nm = names[pc]
  if nm then
    trace[#trace + 1] = string.format("%s cyc=%d  v0=%08x v1=%08x  (dst=%08x)",
          nm, emu.cycles(), emu.reg(2), emu.reg(3),
          (pc == 0x8016d0b4 or pc == 0x8016d1f8) and emu.reg(3) or 0)
    if #trace > 60 then table.remove(trace, 1) end
  end
  emu.resume()
end)

emu.on_event(function(name)
  if name == "dma_ch3_done" then
    trace[#trace + 1] = string.format("ch3DMA cyc=%d", emu.cycles())
    if #trace > 60 then table.remove(trace, 1) end
    return
  end
  if name == "dma_ch3_nomaster" or name == "dma_irq_suppressed" or name == "dma_ch3_raised_unmasked" or name == "dma_ch3_raised_MASKED" or name == "dicr_write_ack" or name == "dicr_write" then
    trace[#trace + 1] = string.format("%s cyc=%d", name, emu.cycles())
    if #trace > 60 then table.remove(trace, 1) end
    return
  end
  if name == "dma_ch3_irq_disabled" then
    trace[#trace + 1] = string.format("ch3-IRQ-OFF cyc=%d", emu.cycles())
    if #trace > 60 then table.remove(trace, 1) end
    return
  end
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if mbs > 0 and (vb - last_mb_vb) == 25 and not dumped then
    dumped = true
    print(string.format("[STOP] vb=%d cyc=%d — last register/DMA events:", vb, emu.cycles()))
    for _, l in ipairs(trace) do print("   " .. l) end
      end
end)

print("[DICR ORDER] armed")
