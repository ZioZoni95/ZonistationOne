-- ps_logo_nclip_check.lua — the logo's GTE transform loop runs (RTPT/NCLIP/
-- AVSZ3/NCDS from ra=0x8004f650/0x8004f668) but corresponding GP0 polygon
-- draws almost never fire. Read MAC0 (GTE data reg 24, signed) right after
-- each NCLIP from that specific call site to see if it's suspiciously
-- one-signed (which would mean every polygon looks "backface" to whatever
-- culling check the BIOS shell does after NCLIP, explaining the near-total
-- lack of draws despite constant transformation).

local armed = false
local checked = 0
local neg = 0
local pos = 0
local zero = 0

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if not armed then return end
    if name ~= "NCLIP" then return end
    if emu.reg(31) ~= 0x8004f650 then return end  -- only the logo's own call site
    checked = checked + 1
    local mac0 = emu.gte_data(24)
    -- Lua integers are 64-bit; gte_read_data_register already sign-extends
    -- MAC0 as int32, so just compare directly.
    if mac0 < 0 then neg = neg + 1
    elseif mac0 > 0 then pos = pos + 1
    else zero = zero + 1 end
    if checked <= 30 or checked % 3000 == 0 then
        print(string.format("[cyc=%d] [nclip #%d] mac0=%d (neg=%d pos=%d zero=%d)",
              emu.cycles(), checked, mac0, neg, pos, zero))
    end
end)

emu.on_break(function(reason)
    if not armed then
        armed = true
        print(string.format("[cyc=%d] === shell entered, checking NCLIP MAC0 sign from the logo's call site ===", emu.cycles()))
    end
    emu.resume()
end)

print("ps_logo_nclip_check.lua armed.")
