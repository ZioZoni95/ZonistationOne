#!/usr/bin/env python3
"""
GDB Python script to debug PS1 emulator IRQ/I_STAT issues.
Usage: gdb -x debug_irq.py ./myps1_emu
"""

import gdb

class IStatWriteBreakpoint(gdb.Breakpoint):
    """Breakpoint on I_STAT writes to trace acknowledge behavior"""
    
    def __init__(self):
        # Break at the I_STAT write handler in interconnect.c
        # We need to find the exact location
        super(IStatWriteBreakpoint, self).__init__(
            "interconnect.c:1108",  # Line with LOG_IRQ_INFO for I_STAT write
            gdb.BP_BREAKPOINT
        )
        self.silent = True
        self.count = 0
        self.max_hits = 50  # Stop after this many to avoid spam
        
    def stop(self):
        self.count += 1
        if self.count > self.max_hits:
            print(f"\n[IRQ DEBUG] Stopping after {self.max_hits} I_STAT writes")
            return True  # Stop execution
            
        try:
            # Get local variables
            value = gdb.parse_and_eval("value")
            inter = gdb.parse_and_eval("inter")
            irq_status_before = gdb.parse_and_eval("inter->irq_status")
            caller_pc = gdb.parse_and_eval("caller_pc")
            
            print(f"\n[IRQ #{self.count}] I_STAT Write:")
            print(f"  Value written: 0x{int(value):08x}")
            print(f"  I_STAT before: 0x{int(irq_status_before):04x}")
            print(f"  Caller PC:     0x{int(caller_pc):08x}")
            
            # Analyze what SHOULD be cleared
            mask = int(value) & 0x7FF
            should_clear = int(irq_status_before) & ~mask
            will_remain = int(irq_status_before) & mask
            
            print(f"  Analysis (Write-0-Clears):")
            print(f"    Mask (value & 0x7FF): 0x{mask:04x}")
            print(f"    Bits to clear:        0x{should_clear:04x}")
            print(f"    Bits remaining:       0x{will_remain:04x}")
            
            if should_clear == 0:
                print(f"  ⚠️  WARNING: Nothing will be cleared!")
                
        except Exception as e:
            print(f"[IRQ DEBUG] Error: {e}")
            
        return False  # Continue execution


class InterruptExceptionBreakpoint(gdb.Breakpoint):
    """Breakpoint when interrupt exception is triggered"""
    
    def __init__(self):
        # Find where interrupt exceptions are raised
        super(InterruptExceptionBreakpoint, self).__init__(
            "cpu.c:cpu_check_interrupts",  # Function that checks interrupts
            gdb.BP_BREAKPOINT
        )
        self.silent = True
        self.count = 0
        self.max_hits = 20
        
    def stop(self):
        self.count += 1
        if self.count > self.max_hits:
            return False  # Just stop counting
            
        try:
            # Try to get interrupt state
            print(f"\n[INT #{self.count}] Interrupt check triggered")
        except:
            pass
            
        return False


class CPUStateCommand(gdb.Command):
    """Print current CPU and IRQ state"""
    
    def __init__(self):
        super(CPUStateCommand, self).__init__("psx-state", gdb.COMMAND_USER)
        
    def invoke(self, arg, from_tty):
        try:
            # Get CPU state
            cpu = gdb.parse_and_eval("cpu_state")
            print("\n=== PSX CPU State ===")
            print(f"  PC:     0x{int(cpu['pc']):08x}")
            print(f"  SR:     0x{int(cpu['sr']):08x}")
            print(f"  Cause:  0x{int(cpu['cause']):08x}")
            print(f"  EPC:    0x{int(cpu['epc']):08x}")
            
            # Get IRQ state
            inter = gdb.parse_and_eval("interconnect_state")
            print("\n=== IRQ State ===")
            print(f"  I_STAT: 0x{int(inter['irq_status']):04x}")
            print(f"  I_MASK: 0x{int(inter['irq_mask']):04x}")
            pending = int(inter['irq_status']) & int(inter['irq_mask'])
            print(f"  Pending: 0x{pending:04x}")
            
            # Decode pending IRQs
            irq_names = ["VBLANK", "GPU", "CDROM", "DMA", "TMR0", "TMR1", "TMR2", "PAD", "SIO", "SPU", "IRQ10"]
            pending_list = [irq_names[i] for i in range(11) if pending & (1 << i)]
            if pending_list:
                print(f"  Active: {', '.join(pending_list)}")
                
        except Exception as e:
            print(f"Error: {e}")


class TraceIRQCommand(gdb.Command):
    """Start tracing IRQ writes"""
    
    def __init__(self):
        super(TraceIRQCommand, self).__init__("psx-trace-irq", gdb.COMMAND_USER)
        self.bp = None
        
    def invoke(self, arg, from_tty):
        if self.bp is None:
            self.bp = IStatWriteBreakpoint()
            print("IRQ tracing enabled. Use 'psx-trace-irq' again to disable.")
        else:
            self.bp.delete()
            self.bp = None
            print("IRQ tracing disabled.")


class WatchIStatCommand(gdb.Command):
    """Set a watchpoint on I_STAT register"""
    
    def __init__(self):
        super(WatchIStatCommand, self).__init__("psx-watch-istat", gdb.COMMAND_USER)
        
    def invoke(self, arg, from_tty):
        try:
            gdb.execute("watch interconnect_state.irq_status")
            print("Watchpoint set on interconnect_state.irq_status")
        except Exception as e:
            print(f"Error: {e}")


class BreakOnIRQCommand(gdb.Command):
    """Break when specific IRQ is pending"""
    
    def __init__(self):
        super(BreakOnIRQCommand, self).__init__("psx-break-irq", gdb.COMMAND_USER)
        
    def invoke(self, arg, from_tty):
        if not arg:
            print("Usage: psx-break-irq <irq_number>")
            print("  IRQ 0: VBLANK, 1: GPU, 2: CDROM, 3: DMA, 4: TMR0...")
            return
            
        try:
            irq_num = int(arg)
            bit_mask = 1 << irq_num
            cond = f"(interconnect_state.irq_status & interconnect_state.irq_mask & {bit_mask}) != 0"
            # Set conditional breakpoint at main loop
            bp = gdb.Breakpoint("cpu_step")
            bp.condition = cond
            print(f"Breakpoint set: will stop when IRQ{irq_num} is pending")
        except Exception as e:
            print(f"Error: {e}")


# Register commands
CPUStateCommand()
TraceIRQCommand()
WatchIStatCommand()
BreakOnIRQCommand()

print("""
=== PSX Emulator IRQ Debug Script Loaded ===
Commands:
  psx-state        - Print CPU and IRQ state
  psx-trace-irq    - Toggle I_STAT write tracing
  psx-watch-istat  - Set watchpoint on I_STAT
  psx-break-irq N  - Break when IRQ N is pending

Quick start:
  (gdb) run
  (gdb) psx-state
  (gdb) psx-trace-irq
  (gdb) continue
""")
