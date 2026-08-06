-- ps_logo_vertex_dump.lua — decode real submitted vertex/color data for every
-- polygon GP0 command completed during the logo's known active cycle window
-- (~186M-271M, per ps_logo_gates_check.lua / ps_logo_find_frame.lua), using
-- the new emu.gp0_opcode()/gp0_word(i) bindings (added specifically for this
-- check) fired from a new "gp0_poly_complete" native probe (right before the
-- draw handler runs, so the command buffer is fully populated).
--
-- GP0 polygon command word layout (DOCS/graphicsprocessingunitgpu.md):
--   word0: cmd/color (bit28 gouraud, bit27 quad, bit26 textured, bit25 semi-transp)
--   per vertex: [Color (if gouraud)] Vertex(YYYYXXXX) [UV (if textured)]

local WINDOW_START = 150000000 -- skip the earlier SONY diamond splash entirely
local count = 0
local minx, maxx, miny, maxy = 32767, -32768, 32767, -32768
local printed = 0

local function sign16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then return v - 0x10000 end
    return v
end

emu.on_event(function(name)
    if name ~= "gp0_poly_complete" then return end
    if printed >= 60 then return end
    local cyc = emu.cycles()
    if cyc < WINDOW_START then return end

    local op = emu.gp0_opcode()
    local gouraud  = (op & 0x10) ~= 0
    local quad     = (op & 0x08) ~= 0
    local textured = (op & 0x04) ~= 0
    local nverts = quad and 4 or 3

    count = count + 1

    local idx = 1 -- word 0 already consumed (color/cmd)
    local color0 = emu.gp0_word(0) & 0xFFFFFF
    local coords = {}
    for v = 1, nverts do
        local col = color0
        if gouraud and v > 1 then
            col = emu.gp0_word(idx) & 0xFFFFFF
            idx = idx + 1
        end
        local vert = emu.gp0_word(idx)
        idx = idx + 1
        if textured then idx = idx + 1 end
        local x = sign16(vert & 0xFFFF)
        local y = sign16((vert >> 16) & 0xFFFF)
        coords[v] = {x = x, y = y, col = col}
        if x < minx then minx = x end
        if x > maxx then maxx = x end
        if y < miny then miny = y end
        if y > maxy then maxy = y end
    end

    -- Skip the per-frame full-screen clear quad (0,0)-(~640,~480/511) — not
    -- interesting, and would otherwise exhaust the print cap immediately.
    -- (Bounding box of THIS polygon's own vertices, not the running global.)
    local px0, px1, py0, py1 = coords[1].x, coords[1].x, coords[1].y, coords[1].y
    for v = 2, nverts do
        if coords[v].x < px0 then px0 = coords[v].x end
        if coords[v].x > px1 then px1 = coords[v].x end
        if coords[v].y < py0 then py0 = coords[v].y end
        if coords[v].y > py1 then py1 = coords[v].y end
    end
    local is_fullscreen_clear = (quad and (px1 - px0) >= 600 and (py1 - py0) >= 400)

    if not is_fullscreen_clear and printed < 60 then
        printed = printed + 1
        local s = string.format("[cyc=%d] #%d op=0x%02x g=%s q=%s t=%s verts:", cyc, count, op,
              tostring(gouraud), tostring(quad), tostring(textured))
        for v = 1, nverts do
            s = s .. string.format(" (%d,%d,#%06x)", coords[v].x, coords[v].y, coords[v].col)
        end
        print(s)
    end
end)

-- Periodic summary via a breakpoint on the post-gate draw call, just to keep
-- the run alive and flush a final report near the end of the window.
emu.add_breakpoint(0x8004f6a4)
local last_report = 0
emu.on_break(function(reason)
    local cyc = emu.cycles()
    if cyc - last_report > 20000000 then
        last_report = cyc
        print(string.format("[cyc=%d] SUMMARY so far: count=%d x=[%d,%d] y=[%d,%d]",
              cyc, count, minx, maxx, miny, maxy))
    end
    emu.resume()
end)

print("ps_logo_vertex_dump.lua armed.")
