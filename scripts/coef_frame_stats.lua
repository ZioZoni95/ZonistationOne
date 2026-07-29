-- Statistics over a WHOLE decoded frame's MDEC coefficient buffer.
-- Sampling single macroblocks was inconclusive (dark regions look identical to
-- a broken decode). Walk the entire buffer: per block, record the DC word and
-- how many AC codes precede the 0xFE00 EOB.

local mb, done = 0, false

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  mb = mb + 1
  if mb < 4200 then return end
  done = true

  local ia = emu.mdec_dma()
  local start
  for k = 0, 0x4000, 4 do
    local w = emu.read_u32(ia - k)
    if (w >> 28) == 3 and (w & 0xffff) > 0x100 then start = ia - k; break end
  end
  if not start then print("[STATS] command word not found"); return end

  local cmd = emu.read_u32(start)
  local nwords = cmd & 0xffff
  print(string.format("[STATS] cyc=%d cmd=0x%08x nwords=%d buf=0x%08x", emu.cycles(), cmd, nwords, start))

  -- halfword accessor over the buffer body
  local function hw(i)
    local w = emu.read_u32(start + 4 + (i >> 1) * 4)
    return (i & 1) == 0 and (w & 0xffff) or ((w >> 16) & 0xffff)
  end

  local total_hw = nwords * 2
  local i, blocks, dchist, achist = 0, 0, {}, {}
  local dcmin, dcmax = 4096, -4096
  local sample = {}
  while i < total_hw do
    local n = hw(i)
    while n == 0xFE00 and i < total_hw do i = i + 1; n = hw(i) end   -- skip padding
    if i >= total_hw then break end
    local qs = (n >> 10) & 0x3F
    local dc = n & 0x3FF
    if dc >= 512 then dc = dc - 1024 end
    i = i + 1
    local acs = 0
    while i < total_hw do
      local c = hw(i); i = i + 1
      if c == 0xFE00 then break end
      acs = acs + 1
      if acs > 70 then break end
    end
    blocks = blocks + 1
    dchist[dc] = (dchist[dc] or 0) + 1
    achist[acs] = (achist[acs] or 0) + 1
    if dc < dcmin then dcmin = dc end
    if dc > dcmax then dcmax = dc end
    if blocks <= 12 then sample[#sample + 1] = string.format("q%d/dc%d/ac%d", qs, dc, acs) end
  end

  print(string.format("[STATS] blocks=%d dc_range=[%d..%d]", blocks, dcmin, dcmax))
  print("[STATS] first blocks: " .. table.concat(sample, " "))
  local keys = {}
  for k in pairs(dchist) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b) return dchist[a] > dchist[b] end)
  for j = 1, math.min(8, #keys) do
    print(string.format("[STATS] DC %5d : %d blocks", keys[j], dchist[keys[j]]))
  end
  local ak = {}
  for k in pairs(achist) do ak[#ak + 1] = k end
  table.sort(ak)
  local parts = {}
  for _, k in ipairs(ak) do parts[#parts + 1] = string.format("%d:%d", k, achist[k]) end
  print("[STATS] AC-count histogram: " .. table.concat(parts, " "))
end)

print("[COEF FRAME STATS] armed")
