-- Is SPU sample generation paced by the guest's clock?
-- Sample production must track *emulated* time: 44100 samples per emulated
-- second, i.e. one per 768 CPU cycles. This compares samples produced against
-- cycles elapsed, and watches the output ring for starvation or overflow.
local SAMPLE_RATE, CYCLES_PER_SAMPLE = 44100, 768
local vb, last_c, last_s = 0, nil, nil

emu.on_event(function(name)
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
      "[SPU vb=%d] %d samples / %d cycles  (expected %d, %+.2f%%)  ring=%d/%d  dropped=%d  keyon=%d",
      vb, ds, dc, expected, err, used, cap, dropped, keyons))
  end
  last_c, last_s = cyc, gen
end)
emu.log("[SPU RATE] armed")
