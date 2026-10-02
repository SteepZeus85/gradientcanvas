<div align="center">

# 🌈 Gradient Canvas

**Animated gradients across your whole setup for [OpenRGB](https://openrgb.org).**

Place your keyboard, fans, strips, RAM and GPU on a map where they physically sit. A single gradient, spiral, wave or plasma then flows across all of them as one picture, instead of each device animating on its own.

[![Build](https://github.com/SteepZeus85/gradientcanvas/actions/workflows/build.yml/badge.svg)](https://github.com/SteepZeus85/gradientcanvas/actions/workflows/build.yml)
![OpenRGB 1.0](https://img.shields.io/badge/OpenRGB-1.0-blueviolet)
![Plugin API v5](https://img.shields.io/badge/plugin%20API-v5-blue)
![License: GPL v2+](https://img.shields.io/badge/license-GPL--2.0--or--later-green)

![Gradient Canvas running a spiral effect across fans, RAM, GPU, keyboard and a case strip](docs/screenshot.webp)

</div>

---

## ✨ Features

### 🎨 Seven spatial effects
| Effect | What it does |
|---|---|
| **Linear Gradient** | A multi-colour gradient sweeping in any direction |
| **Radial** | Rings rippling out from a point you choose |
| **Spiral** | Rotating spiral arms with adjustable twist |
| **Wave** | A travelling band of brightness over drifting colour |
| **Plasma** | Slow, organic, lava-lamp colour blobs |
| **Breathing** | Pulses through your colours, optionally rippling outward |
| **Colour Cycle** | Everything cycles together, or in a gentle spread |

Every effect has speed, size, brightness, mirror (ping-pong) and reverse controls.

### 🗺️ Layout map
- **Drag, resize and rotate** every device zone on a canvas so effects follow your real layout.
- **Move individual LEDs** with *Edit LEDs*: snap to a grid, rubber-band select, and double-click to reset.
- **Arrange LEDs as ring** turns a fan's LED strip into a circle in one click.
- A **live preview** of the effect is drawn behind the map. You can untick it to save CPU.
- Unplugged devices keep their place and pick up again when reconnected.

### 🌈 Gradient editor
- Colours blend in **OKLab**, so transitions stay vivid instead of turning muddy grey.
- Click to add a colour stop, drag to move it, double-click to recolour, right-click to delete.
- 11 presets (Rainbow, Neon Sunset, Cyberpunk, Aurora, Vaporwave…) plus **Randomise**.

### 🎛️ Colour calibration
Different hardware often shows the same colour differently: a "white" that's blue on one strip and pink on a fan. **Calibrate colours…** lights every device with a test colour so you can compare them side by side, then adjust each device's:
- **Red / Green / Blue** balance
- **Brightness**
- **Gamma**
- **Saturation**
- **Channel order** (fixes strips wired as GRB, BRG and so on)

The on-screen preview always shows the colour you intended. Only what's sent to the hardware is corrected.

### 💾 Profiles & saving
- Everything **saves automatically**, including whether the animation was playing.
- Works with **OpenRGB profiles**: each profile can store its own effect and gradient. The layout map and calibration are shared across profiles, because they describe your hardware.
- Loading a profile *without* Gradient Canvas settings pauses the animation, so that profile's own colours show.

### ⚡ Smooth with slow hardware
Colours are sent to devices from a background thread, so slow RAM or motherboard controllers never freeze OpenRGB's window. Each device updates as fast as it can, and fast devices aren't held back by slow ones.

---

## 📥 Installation

> **Requires OpenRGB 1.0** (plugin API v5). Older versions such as 0.9 can't load it.

1. Go to the **[Releases](https://github.com/SteepZeus85/gradientcanvas/releases)** page and download `GradientCanvasPlugin.dll` (Windows) or `libGradientCanvasPlugin.so` (Linux).
2. **Fully close OpenRGB**, including its tray icon.
3. Copy the file into OpenRGB's plugins folder:

   | OS | Folder |
   |---|---|
   | Windows | `%APPDATA%\OpenRGB\plugins\` |
   | Linux / macOS | `~/.config/OpenRGB/plugins/` |

4. Start OpenRGB. A **Gradient Canvas** tab appears along the top.

> 💡 You can also paste `%APPDATA%\OpenRGB\plugins` into the Windows Run box (Win + R) to open the folder.

## 🚀 Quick start

1. Tick devices in the left-hand list, or click **Add all** and then **Auto-arrange**.
2. Drag each device to where it sits on your desk or in your case. Use the round handle above a selected zone to rotate it.
3. For fans, select the zone and click **Arrange LEDs as ring**.
4. Pick an effect and a gradient preset, then press **▶ Play**.
5. If colours don't match between devices, open **Calibrate colours…**.

---

## ❓ Troubleshooting

<details>
<summary><b>The Gradient Canvas tab doesn't appear</b></summary>

- Make sure you're on **OpenRGB 1.0**: check *Information → Software*.
- Check *Settings → Plugins*. If the plugin is listed but disabled, enable it.
- Open the newest log in `%APPDATA%\OpenRGB\logs\` and search for `GradientCanvas`. The message there usually explains the problem, such as an incompatible API version.
- The plugin must be built with the same Qt version as OpenRGB. Release builds match the official OpenRGB 1.0 Windows build (Qt 6.8.3, MSVC 2022). If you built OpenRGB yourself, build the plugin yourself too (see below).
</details>

<details>
<summary><b>A device doesn't change colour</b></summary>

- Make sure **Send to devices** is ticked and the animation is playing.
- Some devices need a *Direct* mode in OpenRGB. The plugin switches to it automatically when one exists.
- Hover over the fps counter to see how long the last device write took. Very slow devices (some RAM over SMBus, for example) simply update less often.
</details>

<details>
<summary><b>Colours look different on each device</b></summary>

Use **Calibrate colours…** in the toolbar. The window explains step by step how to match devices.
</details>

<details>
<summary><b>Where are my settings stored?</b></summary>

`%APPDATA%\OpenRGB\plugins\settings\GradientCanvas.json` (Linux/macOS: `~/.config/OpenRGB/plugins/settings/`). Delete it to reset the plugin.
</details>

---

## 🛠️ Building from source

Every push is built automatically by [GitHub Actions](.github/workflows/build.yml), and publishing a **Release** attaches the built files to it.

To build locally you need the OpenRGB source (for its headers) in an `OpenRGB` subfolder and the **same Qt version** as your OpenRGB.

**Windows** (Qt 6.8.3 MSVC 2022 64-bit + Visual Studio 2022, in an *x64 Native Tools Command Prompt*):
```bat
git clone https://github.com/SteepZeus85/gradientcanvas.git
cd gradientcanvas
git clone https://gitlab.com/CalcProgrammer1/OpenRGB.git OpenRGB
mkdir build && cd build
C:\Qt\6.8.3\msvc2022_64\bin\qmake.exe ..\GradientCanvasPlugin.pro CONFIG+=release
nmake
```

**Linux:**
```sh
git clone https://github.com/SteepZeus85/gradientcanvas.git && cd gradientcanvas
git clone https://gitlab.com/CalcProgrammer1/OpenRGB.git OpenRGB
mkdir build && cd build
qmake6 ../GradientCanvasPlugin.pro && make -j$(nproc)
make install   # copies to ~/.config/OpenRGB/plugins
```

If your OpenRGB isn't the latest, check out the matching commit inside the `OpenRGB` folder first, because the plugin API version must match exactly.

<details>
<summary><b>Project layout</b></summary>

```
src/
  GradientCanvasPlugin.*    plugin entry point, settings file, profiles, device lifecycle
  RenderEngine.*            frame timer + background output thread
  Effects.*                 effect maths
  Gradient.*                OKLab gradient + presets
  Calibration.*             per-zone colour correction
  LayoutModel.*             zone / LED placement and JSON
  ui/CanvasWidget.*         the main tab
  ui/LayoutCanvas.*         the map editor
  ui/GradientEditor.*       gradient stop editor
  ui/CalibrationDialog.*    colour calibration window
```
</details>

---

## 🤝 Contributing

Bug reports, ideas and pull requests are welcome. When reporting a bug, please include:
- your OpenRGB version (*Information → Software*)
- the relevant part of the log from `%APPDATA%\OpenRGB\logs\`
- which devices are involved

## 📜 License

Gradient Canvas is released under the **GNU General Public License v2.0 or later**, the same licence as OpenRGB. See [LICENSE](LICENSE).

Not affiliated with or endorsed by the OpenRGB project. Thanks to the OpenRGB developers for the plugin API that makes this possible.
