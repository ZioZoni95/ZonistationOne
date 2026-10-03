-- Where does the FMV die?
-- Emulated pacing is healthy (~1.37M cycles/frame) and the host runs at ~90%
-- realtime, yet a whole run produces only 46 MDEC frame commands: the movie
-- stops after ~2 seconds and never resumes. This samples per VBlank (new
-- "vblank" probe) and, once MDEC has been idle for a while, reports where the
-- CPU actually sits plus a PC histogram of the wait loop.

local vb, mbs, last_mb_vb, last_mb_cyc = 0, 0, 0, 0
local ups, ch2, ch1 = 0, 0, 0
local reported, hist, hist_n = false, {}, 0
local ring, ring_i = {}, 0

emu.on_event(function(name)
  if name == "mdec_macroblock" then
    mbs = mbs + 1; last_mb_vb = vb; last_mb_cyc = emu.cycles()
    return
  elseif name == "gp0_vram_upload" then ups = ups + 1; return
  elseif name == "dma_ch2_done" then ch2 = ch2 + 1; return
  elseif name == "mdec_ch1_done" then ch1 = ch1 + 1; return
  elseif name ~= "vblank" then return end

  vb = vb + 1
  local pc = emu.pc()
  ring_i = (ring_i % 120) + 1
  ring[ring_i] = string.format("vb%d:%08x", vb, pc)

  if vb % 50 == 0 then
    print(string.format("[VB %4d cyc=%d] mbs=%d ups=%d ch1=%d ch2=%d | last_mb %d vblanks ago | pc=%08x",
          vb, emu.cycles(), mbs, ups, ch1, ch2, vb - last_mb_vb, pc))
  end

  -- stall detected: MDEC silent for 150 VBlanks after having run
  if mbs > 0 and (vb - last_mb_vb) == 150 and not reported then
    reported = true
    print(string.format("[STALL] MDEC idle since vb %d (cyc %d); now vb %d cyc %d",
          last_mb_vb, last_mb_cyc, vb, emu.cycles()))
    local ia, ir, oa, orr = emu.mdec_dma()
    local d, rem, qs = emu.mdec_info()
    local dx, dy, hs, he, ls, le = emu.display_area()
    print(string.format("[STALL] mdec in=0x%08x rem=%d out=0x%08x rem=%d | depth=%d remain_hw=%d",
          ia, ir, oa, orr, d, rem))
    print(string.format("[STALL] display=(%d,%d) h=%d..%d v=%d..%d gpustat=0x%08x",
          dx, dy, hs, he, ls, le, emu.gpustat()))
    print("[STALL] recent PCs: " .. table.concat(ring, " ", 1, math.min(#ring, 40)))
  end

  if reported and hist_n < 400 then
    hist[pc] = (hist[pc] or 0) + 1
    hist_n = hist_n + 1
    if hist_n == 400 then
      local keys = {}
      for k in pairs(hist) do keys[#keys + 1] = k end
      table.sort(keys, function(a, b) return hist[a] > hist[b] end)
      print("[STALL] PC histogram over 400 vblanks:")
      for i = 1, math.min(10, #keys) do
        print(string.format("   %08x : %3d  %s", keys[i], hist[keys[i]], emu.disasm(keys[i])))
      end
      local top = keys[1]
      print("[STALL] context around top PC:")
      for i = -8, 8 do
        print(string.format("     %08x  %s", top + i * 4, emu.disasm(top + i * 4)))
      end
    end
  end
end)

print("[FMV STALL PROBE] armed")
