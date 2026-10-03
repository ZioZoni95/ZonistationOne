-- ps_logo_tmd_read.lua — restart from scratch: we KNOW the PS-logo TMD data
-- (LBA 5-11) lands at RAM 0x00010800-0x00013FFF via CDROM DMA ch3 (confirmed
-- from logs/DMA.log this session). Instead of guessing which BIOS function
-- draws it, watch for the first real CPU read of that buffer — whoever reads
-- it first is very likely the actual TMD-parsing/GTE-transform routine we've
-- been unable to find by following call chains forward.

local hits = 0

emu.add_read_watch(0x80010800)
emu.add_read_watch(0xA0010800)
emu.add_read_watch(0x00010800)
-- Also watch a handful of other offsets within the 7-sector buffer in case
-- parsing code jumps straight to a header field partway through rather than
-- reading byte 0 first.
emu.add_read_watch(0x80010810)
emu.add_read_watch(0x80011000)
emu.add_read_watch(0x80012000)
emu.add_read_watch(0x80013000)

emu.on_break(function(reason)
    if reason:find("Read watchpoint") then
        hits = hits + 1
        if hits <= 25 then
            print(string.format("[TMD-read #%d] %s ra=0x%08x a0=0x%08x a1=0x%08x a2=0x%08x",
                  hits, reason, emu.reg(31), emu.reg(4), emu.reg(5), emu.reg(6)))
        end
        if hits == 1 then
            -- First hit is the interesting one: stay paused here for real
            -- inspection instead of auto-resuming, so Live Disasm/Registers
            -- panels can be used by hand if needed.
            print("=== first TMD read — pausing for inspection ===")
            return
        end
    end
    emu.resume()
end)

print("ps_logo_tmd_read.lua armed — watching first CPU read of RAM 0x80010800 (TMD data, KSEG0).")
