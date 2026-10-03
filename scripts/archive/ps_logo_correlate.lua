-- ps_logo_correlate.lua — direct correlation test: does a real GP0 polygon
-- draw ever follow shortly (same vertex-processing window) after a NCLIP
-- from the logo's own call site (ra=0x8004f650) that returned a POSITIVE
-- MAC0 (the "front-facing, should draw" case for a standard PSX backface
-- cull)? If yes, some draws ARE reaching the GPU and the bug is more subtle
-- (only some fraction lost). If NCLIP-positive never correlates with a
-- gp0_poly shortly after, the drop happens between NCLIP and GP0 submission
-- for essentially all logo polygons.

local armed = false
local last_nclip_mac0 = nil
local last_nclip_cyc = nil
local correlated_hits = 0
local correlated_misses = 0
local nclip_positive_total = 0
local poly_after_positive_examples = 0

emu.add_breakpoint(0x80030000)

emu.on_event(function(name)
    if not armed then return end

    if name == "NCLIP" and emu.reg(31) == 0x8004f650 then
        last_nclip_mac0 = emu.gte_data(24)
        last_nclip_cyc = emu.cycles()
        if last_nclip_mac0 > 0 then
            nclip_positive_total = nclip_positive_total + 1
            if nclip_positive_total % 1000 == 0 then
                print(string.format("[cyc=%d] summary: nclip_positive_total=%d correlated_hits=%d correlated_misses=%d",
                      emu.cycles(), nclip_positive_total, correlated_hits, correlated_misses))
            end
        end
        return
    end

    if name == "gp0_poly" then
        if last_nclip_mac0 and last_nclip_mac0 > 0 and (emu.cycles() - last_nclip_cyc) < 2000 then
            correlated_hits = correlated_hits + 1
            if poly_after_positive_examples < 10 then
                poly_after_positive_examples = poly_after_positive_examples + 1
                print(string.format("[cyc=%d] MATCH: gp0_poly followed positive NCLIP (mac0=%d, %d cycles earlier)",
                      emu.cycles(), last_nclip_mac0, emu.cycles() - last_nclip_cyc))
            end
        else
            correlated_misses = correlated_misses + 1
        end
        return
    end
end)

emu.on_break(function(reason)
    if not armed then
        armed = true
        print(string.format("[cyc=%d] === shell entered, correlating NCLIP(mac0>0) with gp0_poly ===", emu.cycles()))
    end
    emu.resume()
end)

print("ps_logo_correlate.lua armed.")
