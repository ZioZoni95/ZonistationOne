-- MDEC coefficient-stream shape probe.
-- MDEC.log shows every colour macroblock consuming exactly 12 halfwords = 2 per
-- block = "DC word + EOB". Either the game's software VLC decoder really emits
-- DC-only blocks, or we misparse/lose halfwords. This dumps the raw RAM the
-- ch0 DMA is feeding MDEC, the FIFO head, and the resulting RGB.

local mb, dumped = 0, 0

local function hx(v) return string.format("0x%08x", v) end

emu.on_event(function(name)
  if name ~= "mdec_macroblock" then return end
  mb = mb + 1
  if dumped >= 4 then return end
  if mb ~= 1 and mb % 97 ~= 0 then return end
  dumped = dumped + 1

  local ia, irem, oa, orem = emu.mdec_dma()
  local depth, remain, qs = emu.mdec_info()
  print(string.format("[MB %d cyc %d] in_addr=%s in_rem=%d out_addr=%s out_rem=%d depth=%d remain_hw=%d qscale=%d",
        mb, emu.cycles(), hx(ia), irem, hx(oa), orem, depth, remain, qs))

  if dumped == 1 then
    local qy, quv, sc = {}, {}, {}
    for i = 0, 15 do qy[#qy + 1]   = string.format("%02x", emu.mdec_qtable(i, false)) end
    for i = 0, 15 do quv[#quv + 1] = string.format("%02x", emu.mdec_qtable(i, true))  end
    for i = 0, 7  do sc[#sc + 1]   = string.format("%d",  emu.mdec_scale(i))          end
    print("  qt_y[0..15]:  " .. table.concat(qy, " "))
    print("  qt_uv[0..15]: " .. table.concat(quv, " "))
    print("  scale[0..7]:  " .. table.concat(sc, " "))
  end

  -- RAM the ch0 DMA is walking, as halfword pairs (MDEC's consumption unit).
  local base = (ia - 64) & 0xfffffffc
  local line = {}
  for i = 0, 31 do
    local w = emu.read_u32(base + i * 4)
    line[#line + 1] = string.format("%04x %04x", w & 0xffff, (w >> 16) & 0xffff)
    if #line == 8 then
      print(string.format("  ram %s: %s", hx(base + (i - 7) * 4), table.concat(line, " ")))
      line = {}
    end
  end

  local n, f = emu.mdec_in_count(), {}
  for i = 0, math.min(n, 24) - 1 do f[#f + 1] = string.format("%04x", emu.mdec_in_peek(i)) end
  print(string.format("  fifo(%d): %s", n, table.concat(f, " ")))

  local px = {}
  for i = 0, 7 do
    local c = emu.mdec_block(i)
    px[#px + 1] = string.format("%02x/%02x/%02x", c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff)
  end
  print("  rgb[0..7]: " .. table.concat(px, " "))
end)

print("[MDEC COEFF STREAM] armed")
