-- Who writes the MDEC coefficient buffer?
-- The buffer the game DMAs into MDEC contains only "DC word + 0xFE00 EOB" per
-- block (luma DC = -512 = black, chroma DC = 0 = neutral), i.e. the game's
-- software VLC decoder is emitting blank blocks. This finds that decoder: arm
-- write watchpoints on the buffer the first FMV frame used, and report PC/ra
-- plus surrounding disassembly on the next frame's write.

local armed, hits = false, 0
local watch = {}

emu.on_event(function(name)
  if name ~= "mdec_macroblock" or armed then return end
  armed = true
  local ia = emu.mdec_dma()
  -- ia is the ch0 read pointer inside the buffer; watch the command word that
  -- precedes the stream and a couple of points further in.
  watch = { ia - 4, ia + 0x200, ia + 0x600 }
  for _, a in ipairs(watch) do emu.add_write_watch(a) end
  print(string.format("[VLC-WRITER] armed on 0x%08x / 0x%08x / 0x%08x (cyc %d)",
        watch[1], watch[2], watch[3], emu.cycles()))
end)

emu.on_break(function(reason)
  hits = hits + 1
  if hits <= 6 then
    local pc = emu.pc()
    print(string.format("[HIT %d] %s  pc=0x%08x ra=0x%08x cyc=%d", hits, reason, pc, emu.reg(31), emu.cycles()))
    print(string.format("  a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x v1=%08x t0=%08x s0=%08x",
          emu.reg(4), emu.reg(5), emu.reg(6), emu.reg(7), emu.reg(2), emu.reg(3), emu.reg(8), emu.reg(16)))
    for i = -6, 6 do
      local a = pc + i * 4
      print(string.format("    %s %08x  %s", (i == 0) and "->" or "  ", a, emu.disasm(a)))
    end
  end
  if hits >= 6 then
    for _, a in ipairs(watch) do emu.remove_write_watch(a) end
    print("[VLC-WRITER] watches removed")
  end
  emu.resume()
end)

print("[VLC-WRITER] waiting for FMV")
