-- Timer-liveness probe (Phase 3.x — timing gap)
-- Measures whether the game reads timers during the FMV, and whether the
-- counter value is stale between reads (advances far less than the elapsed
-- CPU cycles would imply). DuckStation catches the counter up to the current
-- tick on every read; ours only advances in coarse main-loop chunks.

local reads = { [0] = 0, [1] = 0, [2] = 0 }
local last_cyc = { [0] = 0, [1] = 0, [2] = 0 }
local last_val = { [0] = -1, [1] = -1, [2] = -1 }
local stale_hits = { [0] = 0, [1] = 0, [2] = 0 }
local samples = 0

local function on_read(idx)
    reads[idx] = reads[idx] + 1
    local val, src, en = emu.timer(idx)
    local cyc = emu.cycles()
    -- "stale": consecutive reads at different cycles returning the identical
    -- counter — i.e. the timer did not advance though time passed.
    if last_val[idx] == val and cyc ~= last_cyc[idx] then
        stale_hits[idx] = stale_hits[idx] + 1
    end
    last_val[idx] = val
    last_cyc[idx] = cyc

    samples = samples + 1
    if samples <= 24 then
        print(string.format("  T%d read: counter=%5d src=%d en=%s cyc=%d",
            idx, val, src, tostring(en), cyc))
    end
    if samples % 20000 == 0 then
        print(string.format("[TIMER-LIVE] reads T0=%d T1=%d T2=%d | stale T0=%d T1=%d T2=%d",
            reads[0], reads[1], reads[2], stale_hits[0], stale_hits[1], stale_hits[2]))
    end
end

emu.on_event(function(name)
    if     name == "timer0_read" then on_read(0)
    elseif name == "timer1_read" then on_read(1)
    elseif name == "timer2_read" then on_read(2)
    end
end)

print("[TIMER LIVENESS PROBE] armed")
