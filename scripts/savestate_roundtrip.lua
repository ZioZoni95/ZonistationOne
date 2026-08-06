-- Savestate round-trip check.
--
-- Saves a state a few seconds in, keeps running, then loads it back and keeps
-- running again. What this is actually testing is the SIOI section added with
-- format version 3: the SIO0 protocol state (analog/config mode, the rumble map,
-- the controller and memory-card transfer steps) lives in a file-static inside
-- sio.c, and restoring it has to put the host pointers back or the first byte
-- clocked after the load dereferences an address from the writing process.
--
-- Run: ZS1_LUA_SCRIPT=scripts/savestate_roundtrip.lua ./myps1_emu <bios> --game=<bin>

local PATH   = "savestates/roundtrip.zst"
local SAVE_F = 240    -- ~4 s in: past BIOS handover, pad polling underway
local LOAD_F = 480    -- ~4 s later

local frames = 0

emu.on_event(function(name)
  if name ~= "vblank" then return end
  frames = frames + 1

  if frames == SAVE_F then
    print(string.format("[roundtrip] frame %d: saving %s", frames, PATH))
    emu.save_state(PATH)

  elseif frames == LOAD_F then
    print(string.format("[roundtrip] frame %d: loading %s", frames, PATH))
    emu.load_state(PATH)

  elseif frames == LOAD_F + 180 then
    -- Still ticking three seconds after the load means the restored protocol
    -- state is being clocked without faulting.
    print("[roundtrip] survived 180 frames past the load — OK")
  end
end)
