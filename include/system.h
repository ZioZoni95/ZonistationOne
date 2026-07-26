#ifndef SYSTEM_H
#define SYSTEM_H

/*
 * system.c — core "run one frame" driver.
 *
 * Owns the CPU + event-scheduler timing loop that used to live inline in
 * main.c. main.c is now a thin host shell (SDL/GL/audio/threads + framecap)
 * that calls system_run_frame() once per host frame. Threading is unchanged:
 * this runs on the main thread and spawns nothing.
 */

struct Interconnect;
struct Cpu;

/* Seed the initial scheduled events (VBlank + timers). Call once after the CPU
 * and interconnect are wired up, before the first system_run_frame(). */
void system_init(struct Interconnect* inter, struct Cpu* cpu);

/* Run the emulated machine until the next VBlank (one video frame), or return
 * immediately handling a single debugger step when paused. All device timing
 * advances through the event scheduler off the CPU downcount. */
void system_run_frame(struct Interconnect* inter, struct Cpu* cpu);

#endif // SYSTEM_H
