-- SPU health + boot progress in one instrument.
--
-- Sample production must track *emulated* time: one sample per 768 CPU cycles.
-- The macroblock counter doubles as a boot progress marker: it only moves once
-- the disc has booted far enough to play its FMV, so a run that never moves it
-- never got past the BIOS.
local CYCLES_PER_SAMPLE = 768
local vb, mbs, last_c, last_s = 0, 0, nil, nil

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if vb % 120 ~= 0 then return end

  local cyc = emu.cycles()
  local gen, dropped, used, cap, keyons = emu.spu_stats()
  if last_c then
    local dc = (cyc - last_c) & 0xffffffff
    local ds = gen - last_s
    local expected = dc // CYCLES_PER_SAMPLE
    local err = expected > 0 and (ds - expected) * 100.0 / expected or 0
    emu.log(string.format(
      "[SPU vb=%d mb=%d] %d samples / %d cycles (expected %d, %+.2f%%) ring=%d/%d dropped=%d keyon=%d",
      vb, mbs, ds, dc, expected, err, used, cap, dropped, keyons))
  end
  last_c, last_s = cyc, gen
end)
emu.log("[SPU HEALTH] armed")
