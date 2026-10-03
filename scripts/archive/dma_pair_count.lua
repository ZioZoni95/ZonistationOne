-- Does the game get more ch3 DMA completions than it started transfers?
-- 0x8016d830 = ISR marks descriptor 3 and kicks the payload DMA.
-- 0x8016cbe8 = DMA-IRQ handler marks that descriptor 2 (transfer finished).
-- One extra completion would advance the handler onto a descriptor the ISR has
-- not validated yet - which is exactly the corruption seen (stale +28 word).

local START, DONE = 0x8016d830, 0x8016cbe8
local starts, dones, vb, mbs, last_mb_vb = 0, 0, 0, 0, 0
local trace = {}

emu.add_breakpoint(START)
emu.add_breakpoint(DONE)

emu.on_break(function()
  local pc = emu.pc()
  if pc == START then
    starts = starts + 1
    trace[#trace + 1] = string.format("S #%d cyc=%d desc=%08x", starts, emu.cycles(), emu.read_u32(0x801EB6F8))
  elseif pc == DONE then
    dones = dones + 1
    trace[#trace + 1] = string.format("D #%d cyc=%d idx=%d", dones, emu.cycles(), emu.read_u32(0x801ECFD4))
  end
  if #trace > 40 then table.remove(trace, 1) end
  emu.resume()
end)

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  if vb % 50 == 0 and mbs > 0 then
    print(string.format("[VB %d] mbs=%d idle=%d starts=%d dones=%d diff=%d",
          vb, mbs, vb - last_mb_vb, starts, dones, dones - starts))
  end
  if mbs > 0 and (vb - last_mb_vb) == 30 then
    print("[STOP] last events:")
    for _, l in ipairs(trace) do print("   " .. l) end
  end
end)

print("[DMA PAIR COUNT] armed")
