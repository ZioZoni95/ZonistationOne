-- What does the CD ISR see at the moment the movie is abandoned?
-- Movie on disc: 1905 frames / 127 s. We decode 49 frames (~3 s) and then the
-- game spins in its queue-wait loop while the CDROM keeps delivering sectors.
-- The ISR peeks 3 words of each sector into 0x801df7ec before deciding what to
-- do with it, so logging that buffer per INT1 shows the sector stream exactly
-- as the game sees it, across the transition.

local PEEK = 0x801df7ec
local ring, n, mbs, vb = {}, 0, 0, 0
local last_mb_vb, dumped = 0, false

emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; last_mb_vb = vb; return end
  if name == "vblank" then vb = vb + 1
    if mbs > 0 and (vb - last_mb_vb) == 90 and not dumped then
      dumped = true
      print(string.format("[ABORT] mdec silent 90 vblanks; vb=%d cyc=%d int1=%d", vb, emu.cycles(), n))
      print(string.format("[ABORT] queue idx=%d base=0x%08x", emu.read_u32(0x801ECFDC), emu.read_u32(0x801ECFF0)))
      local first = math.max(1, #ring - 79)
      for i = first, #ring do print("   " .. ring[i]) end
    end
    return
  end
  if name ~= "cdrom_int1" then return end
  n = n + 1
  -- the peek buffer still holds the PREVIOUS sector's first 3 words
  local w0, w1, w2 = emu.read_u32(PEEK), emu.read_u32(PEEK + 4), emu.read_u32(PEEK + 8)
  local entry = string.format("int1 #%d vb=%d cyc=%d peek=%08x %08x %08x  (magic=%04x type=%04x chunk=%d/%d frame=%d)",
        n, vb, emu.cycles(), w0, w1, w2,
        w0 & 0xffff, (w0 >> 16) & 0xffff, w1 & 0xffff, (w1 >> 16) & 0xffff, w2)
  ring[#ring + 1] = entry
  if #ring > 200 then table.remove(ring, 1) end
end)

print("[FMV ABORT POINT] armed")
