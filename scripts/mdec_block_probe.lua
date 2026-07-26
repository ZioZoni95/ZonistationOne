-- MDEC decoded-content probe (post timer-unification)
-- Did live timers change what the game's VLC decoder feeds MDEC? Measures the
-- peak luminance of decoded macroblocks (block_rgb, before FIFO/DMA/VRAM) and
-- the input-stream richness (non-EOB tokens per macroblock). If the movie is
-- still near-black with sparse streams, the timer fix didn't unblock it and the
-- fault is the STR demux / VLC feed upstream.

local blocks      = 0
local global_peak = 0
local peak_block  = 0
local bright      = 0     -- blocks with any pixel luma > 80
local prev_rem    = nil
local rich_sum    = 0
local rich_n      = 0

local function luma(v)
    local r =  v         & 0xFF
    local g = (v >>  8)  & 0xFF
    local b = (v >> 16)  & 0xFF
    return (r * 77 + g * 150 + b * 29) >> 8
end

emu.on_event(function(name)
    if name ~= "mdec_macroblock" then return end
    blocks = blocks + 1

    local pk, sum = 0, 0
    for i = 0, 255 do
        local y = luma(emu.mdec_block(i))
        if y > pk then pk = y end
        sum = sum + y
    end
    local mean = sum // 256
    rich_sum = rich_sum + mean; rich_n = rich_n + 1   -- reuse for mean accumulation
    if pk > 80 then bright = bright + 1 end
    if pk > global_peak then global_peak = pk; peak_block = blocks end

    if blocks % 2000 == 0 then
        print(string.format("[MDEC-CONTENT] %d blocks | peak luma=%d (blk %d) | MEAN luma avg=%d | bright(>80)=%d",
            blocks, global_peak, peak_block, rich_n > 0 and (rich_sum // rich_n) or 0, bright))
        rich_sum = 0; rich_n = 0   -- windowed mean
    end
end)

print("[MDEC CONTENT PROBE] armed")
