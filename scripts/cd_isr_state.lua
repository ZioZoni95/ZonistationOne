-- The CD ISR keeps getting INT1 but stops starting ch3 DMAs (ch3_done freezes
-- while int1 keeps counting). Dump the ISR's decision code and watch the
-- globals it keys on, sampled before and after the stall.

local G = {
  { 0x801ECFDC, "idx" },
  { 0x801ECFE0, "g_fe0" },
  { 0x801ECFE4, "g_fe4" },
  { 0x801ECFE8, "g_fe8" },
  { 0x801ECFEC, "g_fec" },
  { 0x801ECFF0, "ring_base" },
  { 0x801ECFF4, "g_ff4" },
  { 0x801EB6F8, "cur_desc" },
  { 0x801EB700, "g_b700" },
  { 0x801EB6F0, "g_b6f0" },
}

local mbs, vb, last_mb_vb = 0, 0, 0
local dumped_code, snaps = false, 0

local function snap(tag)
  local parts = {}
  for _, g in ipairs(G) do
    parts[#parts + 1] = string.format("%s=%08x", g[2], emu.read_u32(g[1]))
  end
  print(string.format("[%s vb=%d cyc=%d] %s", tag, vb, emu.cycles(), table.concat(parts, " ")))
end

emu.on_event(function(name)
  if name == "mdec_macroblock" then
    mbs = mbs + 1; last_mb_vb = vb
    if mbs == 7000 and not dumped_code then
      dumped_code = true
      print("[CODE] CD sector handler 0x8016d780..0x8016d860:")
      for a = 0x8016d780, 0x8016d860, 4 do print(string.format("   %08x  %s", a, emu.disasm(a))) end
      print("[CODE] DMA completion handler 0x8016e610..0x8016e680:")
      for a = 0x8016e610, 0x8016e680, 4 do print(string.format("   %08x  %s", a, emu.disasm(a))) end
    end
    return
  end
  if name ~= "vblank" then return end
  vb = vb + 1
  local idle = vb - last_mb_vb
  if mbs > 3000 and idle < 3 and vb % 20 == 0 then snap("RUN ") end
  if mbs > 0 and (idle == 10 or idle == 40 or idle == 100 or idle == 300) then snap("STOP") end
end)

print("[CD ISR STATE] armed")
