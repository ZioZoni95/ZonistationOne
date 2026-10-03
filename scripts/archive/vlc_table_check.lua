-- Sanity-check the game's VLC lookup tables.
-- The decoder (0x801659xx-0x80165dxx) indexes two tables passed in a2/a3:
-- 0x801703fc and 0x801803fc. Each entry looks like (code<<16)|bitlength.
-- If a table were corrupt/EOB-filled, every AC lookup would terminate the
-- block — exactly the symptom. Histogram the entries to tell.

local done = false

local function survey(base, name)
  local hist, total, zero = {}, 0, 0
  for i = 0, 16383 do
    local e = emu.read_u32(base + i * 4)
    total = total + 1
    if e == 0 then zero = zero + 1 end
    local code = (e >> 16) & 0xffff
    hist[code] = (hist[code] or 0) + 1
  end
  local keys = {}
  for k in pairs(hist) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b) return hist[a] > hist[b] end)
  print(string.format("[TABLE %s @0x%08x] entries=%d zero=%d distinct_codes=%d", name, base, total, zero, #keys))
  for i = 1, math.min(10, #keys) do
    print(string.format("   code 0x%04x : %d entries (%.1f%%)", keys[i], hist[keys[i]], hist[keys[i]] * 100.0 / total))
  end
  local head = {}
  for i = 0, 11 do head[#head + 1] = string.format("%08x", emu.read_u32(base + i * 4)) end
  print("   first: " .. table.concat(head, " "))
end

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or done then return end
  done = true
  print(string.format("[VLC-TABLES] cyc=%d", emu.cycles()))
  survey(0x801703fc, "A")
  survey(0x801803fc, "B")
end)

print("[VLC TABLE CHECK] armed")
