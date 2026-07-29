-- Late-frame FMV probe.
-- Frame 1 is a near-blank fade-in (nwords=1216, DC-only blocks) so early samples
-- prove nothing. Sample well into the movie: decoded RGB out of MDEC, where the
-- GPU uploads it in VRAM, and where the CRTC is displaying from.

local mb, shown, ups = 0, 0, 0

emu.on_event(function(name)
  if name == "mdec_macroblock" then
    mb = mb + 1
    if mb < 4000 or shown >= 6 or mb % 211 ~= 0 then return end
    shown = shown + 1
    local depth, remain, qs = emu.mdec_info()
    local px, nz, peak = {}, 0, 0
    for i = 0, 255 do
      local c = emu.mdec_block(i)
      local r, g, b = c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff
      if r ~= 0 or g ~= 0 or b ~= 0 then nz = nz + 1 end
      if r > peak then peak = r end
      if g > peak then peak = g end
      if b > peak then peak = b end
      if i < 6 then px[#px + 1] = string.format("%02x/%02x/%02x", r, g, b) end
    end
    local n, f = emu.mdec_in_count(), {}
    for i = 0, math.min(n, 16) - 1 do f[#f + 1] = string.format("%04x", emu.mdec_in_peek(i)) end
    print(string.format("[MB %d cyc %d] depth=%d remain=%d qs=%d nonzero=%d/256 peak=0x%02x rgb=%s",
          mb, emu.cycles(), depth, remain, qs, nz, peak, table.concat(px, " ")))
    print("   fifo: " .. table.concat(f, " "))

  elseif name == "gp0_vram_upload" then
    if mb < 4000 then return end
    ups = ups + 1
    if ups % 120 ~= 1 then return end
    local x, y, w, h = emu.vram_upload_rect()
    local dx, dy, hs, he, ls, le = emu.display_area()
    local st = emu.gpustat()
    -- peak byte actually sitting in VRAM inside that rect
    local peak = 0
    for row = 0, h - 1, 8 do
      for col = 0, w - 1, 2 do
        local v = emu.vram16(x + col, y + row) or 0
        local lo, hi = v & 0xff, (v >> 8) & 0xff
        if lo > peak then peak = lo end
        if hi > peak then peak = hi end
      end
    end
    print(string.format("[UP %d] rect=(%d,%d) %dx%d peak=0x%02x | disp_origin=(%d,%d) lines=%d..%d | gpustat=0x%08x depth24=%d",
          ups, x, y, w, h, peak, dx, dy, ls, le, st, (st >> 21) & 1))
  end
end)

print("[LATE FRAME PROBE] armed")
