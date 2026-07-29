-- Producer/consumer view of the FMV stall.
-- The game spins in a queue-scan loop (idx @0x801ECFDC, base ptr @0x801ECFF0,
-- 32-byte entries) waiting for an entry whose first halfword == 1. Question:
-- does the CDROM still deliver INT1 sectors while it waits (game dropping
-- them) or has delivery stopped (we starve it)?

local vb, mbs, int1, last_int1, last_mb_vb = 0, 0, 0, 0, 0
local stalled, dumps = false, 0

local function dump_queue(tag)
  local idx  = emu.read_u32(0x801ECFDC)
  local base = emu.read_u32(0x801ECFF0)
  print(string.format("%s queue idx=%d base=0x%08x", tag, idx, base))
  if base < 0x80000000 or base >= 0x80200000 then return end
  for i = 0, 7 do
    local e = base + i * 32
    local w = {}
    for j = 0, 7 do w[#w + 1] = string.format("%08x", emu.read_u32(e + j * 4)) end
    print(string.format("   [%d]%s 0x%08x: %s", i, (i == idx) and "*" or " ", e, table.concat(w, " ")))
  end
end

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end
  if name == "cdrom_int1" then int1 = int1 + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1

  if vb % 50 == 0 then
    print(string.format("[VB %4d cyc=%d] mbs=%d int1=%d (+%d/50vb) idle=%d pc=%08x",
          vb, emu.cycles(), mbs, int1, int1 - last_int1, vb - last_mb_vb, emu.pc()))
    last_int1 = int1
  end

  if mbs > 0 and (vb - last_mb_vb) == 120 and not stalled then
    stalled = true
    print(string.format("[STALL] at vb %d cyc %d — int1 so far %d", vb, emu.cycles(), int1))
    dump_queue("[STALL]")
  end
  if stalled and dumps < 3 and (vb - last_mb_vb) % 300 == 0 then
    dumps = dumps + 1
    print(string.format("[STALL+%d] vb %d int1=%d", dumps, vb, int1))
    dump_queue("[STALL+]")
  end
end)

print("[FMV STALL 2] armed")
