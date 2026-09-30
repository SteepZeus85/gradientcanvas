# Gradient Canvas: an OpenRGB plugin

Gradient Canvas adds animated gradients, spirals, waves and plasma effects to OpenRGB. It draws them across a **2D layout map** of all your hardware. You place your keyboard, strips, fans, RAM and GPU on the map where they physically sit, and a gradient then sweeps across the whole setup as one continuous picture. Without the map, each device would animate on its own.

Targets **OpenRGB 1.0** (plugin API **v5**). Tested against OpenRGB master as of 30 Sep 2026.

## Features

### Effects
| Effect | What it does | Extra controls |
|---|---|---|
| Linear Gradient | Multi-stop gradient scrolling in any direction | Size, angle |
| Radial | Rings expanding from a centre point | Size, centre |
| Spiral | Rotating spiral arms | Size, arms, twist, centre |
| Wave | Travelling brightness wave over a slowly drifting gradient | Size, angle, depth, sharpness |
| Plasma | Organic sine-plasma through your gradient | Size, turbulence |
| Breathing | Pulses while stepping through gradient colours, with an optional ripple from the centre | Ripple, breaths/loop, centre |
| Colour Cycle | Whole setup cycles through the gradient, with an optional spatial spread | Angle, spread |

All effects share **speed**, **brightness**, **mirror** (ping-pong instead of wrap) and **reverse**.

### Gradient editor
* Blends in **OKLab**, so colour transitions stay vivid and don't pass through muddy greys.
* Click the bar to add a stop, drag a stop to move it, double-click to recolour, right-click to delete.
* 11 presets (Rainbow, Neon Sunset, Ocean, Cyberpunk, Fire, Aurora, Vaporwave…) plus a **Randomise** button.

### Layout map
* **Device placement:** tick zones in the device list and they appear on the canvas. Drag to move, use the corner handle to resize, and the top handle to rotate (hold Shift to snap to 15°). You can also type exact X/Y/W/H/rotation values.
* **Per-LED editing:** select a zone and press **Edit LEDs**. Every LED becomes a draggable dot.
  * Matrix zones snap to their own cell grid. Other zones snap to the canvas grid.
  * Rubber-band select several LEDs to move them together.
  * Double-click an LED to reset it. Moved LEDs get an orange ring.
  * **Arrange LEDs as ring** lays a fan's LEDs out in a circle in one click.
  * Useful for case strips that turn corners, odd keyboard layouts, or any hand-placed LEDs.
* A live preview of the effect is drawn behind the map. The ⊕ marker sets the centre for radial effects (you can also right-click anywhere on the canvas), and an arrow shows the direction of directional effects.
* **Auto-arrange** stacks the placed zones tidily. **Add all** places every zone.
* If a device is unplugged, its placement is kept (shown as *offline*) and it reconnects automatically when the device comes back.

### Other
* Choose between **Play/Pause** and **Send to devices**, so you can design with the on-screen preview without driving the LEDs.
* Settings save automatically, and the full setup (layout, effect and gradient) is stored in OpenRGB **profiles**.
* Handles rescans and hot-plugs safely: it releases every controller when detection starts and rebinds when detection completes.

## Building

The plugin must be built with the **same Qt version (major.minor) and compiler** as the OpenRGB it will load into. Otherwise OpenRGB will refuse to load it or crash. Official OpenRGB 1.0 Windows builds use **Qt 6.8.3 + MSVC 2022 x64**.

### Easiest: GitHub Actions (no local toolchain)
1. Push this folder to a GitHub repo.
2. The included workflow (`.github/workflows/build.yml`) builds Windows and Linux binaries.
3. Download `GradientCanvasPlugin-Windows-x64` from the run's artifacts.

If you use an older or pipeline OpenRGB build, run the workflow manually and set `openrgb_ref` to the OpenRGB commit your build came from. The plugin API version must match exactly.

First fetch the OpenRGB headers into an `OpenRGB` subfolder:
```sh
git clone https://gitlab.com/CalcProgrammer1/OpenRGB.git OpenRGB
```
(Or add it as a submodule if you keep the plugin in git.) Check out the commit your OpenRGB build came from if it isn't the latest.

### Windows (local)
Install Qt 6.8.3 (MSVC 2022 64-bit) and Visual Studio 2022, then run this in an *x64 Native Tools Command Prompt*:
```bat
mkdir build && cd build
C:\Qt\6.8.3\msvc2022_64\bin\qmake.exe ..\GradientCanvasPlugin.pro CONFIG+=release
nmake
```

### Linux
```sh
mkdir build && cd build
qmake6 ../GradientCanvasPlugin.pro && make -j$(nproc)
make install   # copies to ~/.config/OpenRGB/plugins
```

## Installing

Copy the built library into OpenRGB's plugins folder:

| OS | Folder |
|---|---|
| Windows | `%APPDATA%\OpenRGB\plugins\` |
| Linux | `~/.config/OpenRGB/plugins/` |
| macOS | `~/.config/OpenRGB/plugins/` |

Restart OpenRGB. A **Gradient Canvas** tab appears along the top. You can enable or disable the plugin under *Settings → Plugins*.

## Quick start
1. In the device list, tick the zones you want (or click **Add all**, then **Auto-arrange**).
2. Drag each zone to where it physically sits. Use the rotate handle for vertical strips.
3. For fans, select the zone and click **Arrange LEDs as ring**. For bent strips, press **Edit LEDs** and drag the dots into shape (resize the zone first if you need more room, since LEDs stay inside their zone's rectangle).
4. Pick an effect and a gradient preset, then press **Play**.

## How it works
* Every LED gets a world position: its zone's rectangle, rotated about its centre, plus the LED's local position within it. That local position comes from the device's matrix map, an even spread along a strip, or your override.
* Each frame, the active effect is evaluated at that point (aspect-corrected, so circles stay round). The colours are written with `SetColor()` and pushed with `UpdateLEDs()`, which is asynchronous in OpenRGB 1.0.
* Devices are switched to their *Direct* (or *Custom/Static*) per-LED mode with `SetCustomMode()` when playback starts.

## Project layout
```
src/
  GradientCanvasPlugin.*   plugin entry point, settings, profiles, device lifecycle
  RenderEngine.*           frame timer, drives devices
  Effects.*                effect maths
  Gradient.*               OKLab gradient + presets
  LayoutModel.*            zone/LED placement model and JSON
  ui/CanvasWidget.*        main tab
  ui/LayoutCanvas.*        QGraphicsView map editor
  ui/GradientEditor.*      gradient stop editor
```

License: GPL-2.0-or-later, the same as OpenRGB.
