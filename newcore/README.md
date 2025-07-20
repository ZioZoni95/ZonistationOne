# newcore PlayStation 1 Emulator (Refactor)

## Project Status (June 2024)

This project is a modern, modular refactor of a PlayStation 1 emulator, inspired by the structure and best practices of PCSX ReARMed. All major subsystems are scaffolded, integrated, and tested with a focus on extensibility and maintainability.

### **Implemented Subsystems**
- **Logging**: Modular, multi-level logging system.
- **RAM & VRAM**: Initialization, basic read/write, bounds checking.
- **Interconnect**: Basic structure, address masking, RAM attachment.
- **DMA**: Initialization, channel setup.
- **CPU**: Initialization, register state, instruction cache structure.
- **BIOS**: File loading, basic 32/16-bit read.
- **GPU**: Initialization, VRAM integration.
- **Timers**: Initialization, basic state.
- **CDROM**: Initialization, basic state.
- **GTE**: Initialization, register state.
- **Event Scheduler**: Basic event scheduling API.
- **Debugger**: Initialization, basic state.
- **SIO (Serial I/O)**: Initialization, basic state.
- **SPU (Sound Processing Unit)**: Initialization, basic state.
- **Renderer**: **Plugin-based system** (like PCSX ReARMed). The core loads the renderer via a clean API (`renderer_plugin.h`).

### **Architecture**
- **Highly modular**: Each subsystem is in its own directory/file, with clear APIs.
- **Plugin-based renderer**: Renderer is accessed via a function pointer API, allowing for easy swapping or upgrading of rendering backends.
- **Ready for further development**: All stubs and initialization are in place for deeper emulation logic.

### **Build & Run**
- The project builds and runs cleanly (`make -C newcore`).
- Each subsystem logs its initialization and test output.
- The renderer plugin system is working, with stubbed calls and logging.

### **Next Steps**
- Implement real emulation logic for each subsystem.
- Expand the renderer plugin system (add OpenGL, Vulkan, etc.).
- Add more plugin types (input, audio, etc.) if desired.
- Integrate or port advanced features from `pcsx_rearmed_reference` or other sources.

---

**This project is an ideal starting point for further PlayStation 1 emulator development, experimentation, or collaboration.**

If you have questions or want to contribute, see the code comments and modular structure for guidance!
