-- engine_divergence.lua — which subsystem moves first when two CPU engines disagree?
--
-- The golden trace answers "did the machine change" and localises it to a
-- checkpoint interval, but it hashes CPU state only, so when it says the block
-- engine diverged it cannot say whether the cause was the CPU, an interrupt that
-- landed early, a timer, the GPU or the drive.
--
-- This writes one line per field with a fingerprint of each subsystem separately.
-- Run it once per engine and diff the two files: the first differing line is the
-- field the machines parted company, and the first differing *column* on that
-- line is the subsystem that moved. That is a diff a person can read, which a
-- 64-bit hash is not.
--
--   ZS1_CPU=interpreter ZS1_CD_SYNC=1 ZS1_LUA_SCRIPT=scripts/engine_divergence.lua ...
--   grep -o "DIV .*" logs/Debug.log > /tmp/interp.txt
--   ZS1_CPU=blocks      ZS1_CD_SYNC=1 ZS1_LUA_SCRIPT=scripts/engine_divergence.lua ...
--   grep -o "DIV .*" logs/Debug.log > /tmp/blocks.txt
--   diff /tmp/interp.txt /tmp/blocks.txt | head
--
-- ZS1_CD_SYNC is not optional: without it the drive answers on host file I/O and
-- every run differs from every other for reasons that have nothing to do with the
-- CPU (see docs/GOLDEN_TRACE_2026-08-29.md).
--
-- The output path comes from ZS1_DIVERGE_OUT so the two runs cannot overwrite
-- each other; it defaults to the interpreter's name because that is the one
-- recorded first.

-- Output goes through emu.log rather than a file: the Lua state here registers
-- _G, table, string, math and os, and deliberately not io (lua_debug.c) — a
-- debug script writing arbitrary files is not something to add for one probe.
-- Every line is prefixed so it can be pulled out of logs/Debug.log with grep.
local stop  = tonumber(os.getenv("ZS1_DIVERGE_FIELDS") or "4000")
local field = 0
local done  = false

-- A cheap fold over the register file. The whole point is to notice a change,
-- not to identify it, and 32 words per field is too much to diff by eye.
local function regfold()
    local h = 2166136261
    for i = 0, 31 do
        h = (h ~ emu.reg(i)) & 0xFFFFFFFF
        h = (h * 16777619) & 0xFFFFFFFF
    end
    return h
end

emu.on_event(function(name)
    if name ~= "vblank" then return end
    if done then return end
    field = field + 1

    local ist, imask, sr, cause = emu.irq()
    local t0 = emu.timer(0)
    local t1 = emu.timer(1)
    local t2 = emu.timer(2)
    local depth, remain, qscale = emu.mdec_info()
    local gen, dropped, used, _, keyon = emu.spu_stats()

    emu.log(string.format(
        "DIV f=%-6d cyc=%-12d pc=%08X regs=%08X irq=%04X/%04X sr=%08X cause=%08X "..
        "t=%d/%d/%d gpustat=%08X mdec=%d/%d/%d spu=%d/%d/%d",
        field, emu.cycles(), emu.pc(), regfold(),
        ist, imask, sr, cause,
        t0, t1, t2,
        emu.gpustat(),
        depth, remain, qscale,
        gen, dropped, keyon))

    if field >= stop then done = true end
end)
