-- Why does the BIOS report "VSync: timeout"?
--
-- The chain is: VBlank event -> IRQ0 latched in I_STAT -> CPU takes the
-- interrupt (SR.IEc and SR.IM2 set, Cause.IP2 set) -> kernel handler -> the
-- library's vblank counter advances. This samples that chain once per vblank
-- and reports where it stops.
local vb, taken, latched_at_vb, masked = 0, 0, 0, 0
local last_pc_in_handler = 0

emu.on_event(function(name)
  if name ~= "vblank" then return end
  vb = vb + 1
  local stat, mask, sr, cause = emu.irq()
  -- sampled right after the event handler raised IRQ0
  if (stat & 1) ~= 0 then latched_at_vb = latched_at_vb + 1 end
  if (mask & 1) == 0 then masked = masked + 1 end
  if vb % 60 ~= 0 then return end
  emu.log(string.format(
    "[VSYNC vb=%4d] I_STAT=%04x I_MASK=%04x SR=%08x (IEc=%d IM2=%d) Cause.IP=%02x"
    .. " | vb with IRQ0 still latched: %d | vb with IRQ0 masked: %d | pc=%08x",
    vb, stat, mask, sr, sr & 1, (sr >> 10) & 1, (cause >> 8) & 0xFF,
    latched_at_vb, masked, emu.pc()))
end)
emu.log("[VSYNC PROBE] armed")
