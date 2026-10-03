-- Is the CD-DMA completion interrupt being lost?
-- The movie's descriptor goes 3 ("ch3 DMA started") -> 2 ("DMA completed",
-- written from the DMA-IRQ handler at 0x8016e668) -> 4 (decoding). After ~47
-- frames descriptors stay at 3, so the completion notification stops arriving.
-- Count ch3 completions against suppressed DMA IRQs across the transition.

local ch3, supp, disab, mbs, vb, last_mb_vb = 0, 0, 0, 0, 0, 0
local p_ch3, p_supp = 0, 0

emu.on_event(function(name)
  if name == "dma_ch3_done" then ch3 = ch3 + 1; return end
  if name == "dma_irq_suppressed" then supp = supp + 1; return end
  if name == "dma_ch3_irq_disabled" then disab = disab + 1; return end
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if vb % 25 == 0 then
    print(string.format("[VB %4d cyc=%d] mbs=%d idle=%4d | ch3_done=%d (+%d) suppressed=%d (+%d) ch3_irq_disabled=%d",
          vb, emu.cycles(), mbs, vb - last_mb_vb, ch3, ch3 - p_ch3, supp, supp - p_supp, disab))
    p_ch3, p_supp = ch3, supp
  end
end)

print("[DMA IRQ AUDIT] armed")
