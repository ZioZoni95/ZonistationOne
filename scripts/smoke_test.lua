-- smoke_test.lua — verifies the Lua console pipeline end to end.
print("hello from lua")
print(string.format("pc at load time = 0x%08x", emu.pc()))

emu.add_breakpoint(0xBFC00000)  -- reset vector, guaranteed first-instruction hit
emu.add_write_watch(0x00000100) -- low RAM, touched by the BIOS's early RAM clear

local bp_hit, wp_hit = false, false

emu.on_break(function(reason)
    print(string.format("[BREAK] %s pc=0x%08x paused=%s", reason, emu.pc(), tostring(emu.is_paused())))
    if reason:find("Breakpoint") and not bp_hit then
        bp_hit = true
        emu.remove_breakpoint(0xBFC00000)
        emu.resume()
        return
    end
    if reason:find("Write watchpoint") and not wp_hit then
        wp_hit = true
        emu.remove_write_watch(0x00000100)
        emu.resume()
        return
    end
    emu.resume()
end)

print("smoke_test.lua armed.")
