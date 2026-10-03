# Renderers and controllers

Moved out of `README.md`; the text is unchanged.

## Renderers

Two backends, the same pixels. OpenGL 3.3 is the default; Vulkan 1.3 is there when the build found
its headers and a shader compiler.

```sh
ZS1_GFX=gl      ./ZoniStation_One roms/bios-pal.bin --game="games/game.bin"   # default
ZS1_GFX=vulkan  ./ZoniStation_One roms/bios-pal.bin --game="games/game.bin"
```

Or change it without restarting: **Esc → Video** in the gameplay shell lists both backends and the
GPUs each one offers, says which is running, and switches on the spot. The switch rebuilds the SDL
window and the graphics device — SDL fixes `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN` when a window
is created and neither can be added later, so a new window is unavoidable — while three things are
carried across it:

- **VRAM**, read back into host memory before the device goes and pushed into the new one after.
  It has to be read from the GPU: `gpu.vram.data` holds what the CPU wrote, never what the
  rasteriser drew.
- **The drawing state** — draw offset, drawing area, texture window, display region, the mask flags.
  These live in the backend, and the guest has no reason to re-send `GP0(E2..E6)` just because the
  host changed graphics API.
- **The ImGui context** — fonts, `imgui.ini`, the docking layout, the pinned watches. Only the two
  backend halves are rebuilt, so the workspace comes back exactly as it was.

The emulated machine is not involved at all: no reset, no save state, and the CPU, the SPU and the
drive never learn that anything happened. If the new backend fails to come up the old one is
rebuilt and the interface says so.

**Picking a GPU** differs between the two, and the interface says which is which rather than hiding
it. Vulkan enumerates real devices and switches between them live. OpenGL cannot: the GLX vendor
library is resolved at the first `dlopen` of libGL, so its two PRIME choices are listed marked
*next launch* and set through `ZS1_GPU` for the run after.

Two platform limits worth knowing before reporting a bug:

- **The OpenGL backend needs X11 here.** Under `SDL_VIDEODRIVER=wayland` GLEW fails to initialise
  ("Unknown error") and the backend refuses to come up — including as the target of a hot switch,
  which then rolls back to Vulkan and says so.
- **Vulkan on an Intel iGPU needs Wayland**, for the opposite reason: with the X screen owned by
  the NVIDIA driver in dGPU mode, `vkCreateSwapchainKHR` returns `VK_ERROR_INITIALIZATION_FAILED`
  even though the surface reports itself supported.

## Controllers

A DualShock 4 over USB or Bluetooth is picked up automatically, hot-plug included, and the keyboard
stays live beside it (`WASD` D-pad, `E C Z X` for △○×□, `Q R` shoulders, `Shift Ctrl` triggers,
`Space` START, `Tab` SELECT).

The emulated pad boots **digital with its LED off**, as a real one does, and waits for its Analog
button. **F12**, or a click on the DS4 touchpad, is that button:

```
digital (ID 41h, LED off) → analog pad (73h, LED red) → analog stick (53h, LED green) → digital
```

The green stick mode is what a few flight titles expect instead of a DualShock, Ace Combat 2 among
them. `ZS1_PAD_MODE=digital|analog|stick` sets the boot mode; the Controller window in the debug UI
shows the current one and cycles it. A game that drives the pad itself through the config commands
overrides all of this, as on hardware.

Booting analog instead was tried, so that the sticks would reach a game without a keypress first. It
costs more than it saves: the BIOS shell's own pad driver does not cope with ID `73h`, never finishes
initialising, and its main menu comes up without a selection cursor. The hardware default is the
default for a reason.

A DS4's light bar shows those colours, so the mode a game selected is visible on the pad. It needs
access to the pad's hidraw node, which is root-only by default; without it SDL uses the kernel evdev
path — buttons, sticks and rumble work, the light bar does not.

```sh
echo 'KERNEL=="hidraw*", ATTRS{idVendor}=="054c", MODE="0660", TAG+="uaccess"' \
  | sudo tee /etc/udev/rules.d/99-sony-hidraw.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Replug the pad afterwards.

Button bits are the same in every region — the pad's bottom button is × (bit 14) and its right button
○ (bit 13) worldwide. What varies is the software: the PS1 shell and Japanese-developed titles confirm
with ○, most Western ones with ×. `ZS1_PAD_SWAP_XO=1`, or the checkbox in the Controller window,
swaps which physical button drives which bit if you prefer × to confirm everywhere.
