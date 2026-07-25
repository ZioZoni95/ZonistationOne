-- MDEC / FMV watch (Phase 2.8)
-- Correlates MDEC ch1 completions and GP0(0xA0) VRAM uploads against the
-- renderer's staging-pool usage, to find out whether decoded FMV strips are
-- reaching the display path or being dropped for lack of pool space.

local ch1_done  = 0
local uploads   = 0
local up_bytes  = 0
local copies    = 0
local fulls     = 0
local frame_copies = 0
local burst_dumps  = 0
local starts       = 0
local start_dumps  = 0
local last_cyc  = 0
local shown     = 0
local REPORT_EVERY = 60000000   -- cycles

local function report(tag)
    local used, peak, updates, skips = emu.gpu_pool()
    local s = emu.gpustat()
    print(string.format(
        "[FMV %s] cyc=%d ch1=%d starts=%d uploads=%d up_kb=%d copies=%d fulls=%d | pool used=%dKB peak=%dKB updates=%d skips=%d | depth=%s",
        tag, emu.cycles(), ch1_done, starts, uploads, up_bytes // 1024, copies, fulls,
        used // 1024, peak // 1024, updates, skips,
        ((s >> 21) & 1) == 1 and "24bpp" or "15bpp"))
end

emu.on_event(function(name)
    if name == "mdec_ch1_done" then
        ch1_done = ch1_done + 1
    elseif name == "gp0_image_start" then
        starts = starts + 1
        local x, y, w, h = emu.vram_upload_rect()
        if start_dumps < 12 and ((emu.gpustat() >> 21) & 1) == 1 then
            start_dumps = start_dumps + 1
            print(string.format("[A0 START] cyc=%d rect=(%d,%d) %dx%d -> %d words",
                emu.cycles(), x, y, w, h, (w * h + 1) // 2))
        end
        return
    elseif name == "gp0_vram_copy" then
        copies = copies + 1
        frame_copies = frame_copies + 1
        -- Dump the actual GP0(0x80) operands for the first few copies of a
        -- burst: src/dst/size tell us whether the game really asked for this
        -- or our dispatch is replaying one command.
        if frame_copies <= 4 and burst_dumps < 40 then
            burst_dumps = burst_dumps + 1
            local src, dst, dim = emu.gp0_word(1), emu.gp0_word(2), emu.gp0_word(3)
            print(string.format("[COPY] cyc=%d src=(%d,%d) dst=(%d,%d) %dx%d",
                emu.cycles(), src & 0x3FF, (src >> 16) & 0x1FF,
                dst & 0x3FF, (dst >> 16) & 0x1FF,
                dim & 0x3FF, (dim >> 16) & 0x1FF))
        end
        return
    elseif name == "vram_full_upload" then
        -- One per host frame (end-of-frame full upload) — use it as a frame marker.
        fulls = fulls + 1
        if frame_copies > 50 then
            print(string.format("[BURST] cyc=%d %d VRAM copies in one frame", emu.cycles(), frame_copies))
        end
        frame_copies = 0
        return
    elseif name == "gp0_vram_upload" then
        local x, y, w, h = emu.vram_upload_rect()
        uploads  = uploads + 1
        up_bytes = up_bytes + w * h * 2
        -- Sample the pool right at upload time while in 24bpp (the FMV window):
        -- if `used` is already near the cap here, the strips are being dropped.
        if ((emu.gpustat() >> 21) & 1) == 1 and shown < 20 then
            shown = shown + 1
            local used, peak, updates, skips = emu.gpu_pool()
            print(string.format(
                "[FMV UPLOAD] cyc=%d rect=(%d,%d) %dx%d hw | pool used=%dKB updates=%d skips=%d",
                emu.cycles(), x, y, w, h, used // 1024, updates, skips))
        end
        return
    else
        return
    end

    local c = emu.cycles()
    if c - last_cyc >= REPORT_EVERY then
        last_cyc = c
        report("tick")
    end
end)

print("[FMV] armed")
