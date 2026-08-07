-- Reverb boot-jingle trace.
--
-- The PS1 boot chord rings with a long reverb tail. This tells two failure
-- modes apart:
--   (a) the game/BIOS switches reverb off early     -> control/vol/EON change
--   (b) the reverb network's own tail decays wrong   -> out| collapses while
--                                                        rev_en still true
--
-- Run:  ZS1_LUA_SCRIPT=scripts/reverb_boot_trace.lua \
--         ./ZoniStation_One roms/SCPH-7502.BIN
-- or paste in the Script (F8) console and Load & Run, then reboot the BIOS.

local frame = 0
local last_k = nil

emu.on_event(function(name)
  if name ~= "vblank" then return end
  frame = frame + 1

  local control, en, vl, vr, eon, base, cur, inl, inr, outl, outr = emu.reverb()
  local _, _, _, _, kon = emu.spu_stats()
  local om = math.max(math.abs(outl), math.abs(outr))   -- reverb output magnitude
  local im = math.max(math.abs(inl),  math.abs(inr))    -- reverb input magnitude

  -- Decisive: log every change of the control/volume/EON/base that would mute
  -- the unit, with the cycle and key-on count so it can be lined up with the
  -- chord starting.
  local k = string.format("%04x|%d|%d|%d|%08x|%04x", control, en and 1 or 0, vl, vr, eon, base)
  if k ~= last_k then
    last_k = k
    emu.log(string.format("[REV f%d cyc%d] SPUCNT=%04x rev_en=%s vol=(%d,%d) EON=%08x base=%04x kon=%d",
      frame, emu.cycles(), control, tostring(en), vl, vr, eon, base, kon))
  end

  -- Envelope: every 10 frames while any reverb energy exists, so the tail's
  -- decay shape (or its sudden collapse) is visible.
  if frame % 10 == 0 and (om > 0 or im > 0) then
    emu.log(string.format("   f%d in|=%d out|=%d cur=%d", frame, im, om, cur))
  end
end)

emu.log("[REV] boot-reverb trace armed — boot to the logo and read the lines.")
