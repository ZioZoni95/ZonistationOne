-- Scanout audit: what CRTC window does the display pass use during the FMV,
-- and how many GP0(A0) uploads does the CPU thread record per displayed frame?
-- Pairs with the GPU-thread dump (ZS1_DUMP_FRAME) which reports how many of
-- those rects actually reached vram_tex.
local vb, mbs, ups, ups_frame = 0, 0, 0, 0
emu.on_event(function(name)
  if name == "mdec_macroblock" then mbs = mbs + 1; return end
  if name == "gp0_vram_upload" then ups = ups + 1; ups_frame = ups_frame + 1; return end
  if name ~= "vblank" then return end
  vb = vb + 1
  local n = ups_frame; ups_frame = 0
  if vb % 60 ~= 0 then return end
  local dx, dy, hs, he, ls, le = emu.display_area()
  local stat = emu.gpustat()
  local used, peak, updates, skips = emu.gpu_pool()
  emu.log(string.format(
    "[VB %5d] mb=%d ups=%d (last frame %d) | vram_start=(%d,%d) hrange=%d..%d (%d) vrange=%d..%d (%d)"
    .. " | GPUSTAT=%08x vres=%d pal=%d d24=%d inter=%d | pool used=%dK peak=%dK upd=%d skips=%d",
    vb, mbs, ups, n, dx, dy, hs, he, he - hs, ls, le, le - ls,
    stat, (stat >> 19) & 1, (stat >> 20) & 1, (stat >> 21) & 1, (stat >> 22) & 1,
    used // 1024, peak // 1024, updates, skips))
end)
emu.log("[SCANOUT AUDIT] armed")
