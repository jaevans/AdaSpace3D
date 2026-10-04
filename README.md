# AdaSpace3D 🚀

**The definitive firmware upgrade for your DIY SpaceMouse.**

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/) ![Platform](https://img.shields.io/badge/platform-RP2040-red.svg) ![Status](https://img.shields.io/badge/status-Stable-green.svg)

**AdaSpace3D** is a drop-in firmware replacement for any DIY SpaceMouse using an **RP2040** and **TLx493D** sensor.

It fixes the biggest issue with previous DIY firmwares: **It replaces glitchy mouse/keyboard emulation with Native 3DConnexion Driver Support.**

Your DIY build will now be recognized by Windows/macOS as a genuine **SpaceMouse Pro Wireless**. No more glitchy shortcuts, no more keybinding headaches—just buttery smooth 5DOF navigation in 3D software (Fusion360, OrcaSlicer, Blender, etc.).

---

## 💬 Support & Community

I am still learning the ropes here on GitHub, so **support is offered mainly on Discord**.

This is also the place where we hang out to talk, make suggestions, and dream about the new upcoming **v2 SpaceMouse**!

👉 **[Join the Discord Server](http://dsc.gg/axiom3d)**


---

## ✨ Features

* **Native Driver Support:** Emulates the official 3DConnexion USB protocol. Works out-of-the-box with standard drivers.
* **Unified Firmware:** One file for everyone. The code **automatically detects** if your sensor is connected via **Stemma QT (Cable)** or **Soldered Headers**.
* **Reactive Lighting:**
    * **Dual Drive:** Supports both Addressable (NeoPixel) and Standard LEDs simultaneously.
    * **Smart Feedback:** LED glows dim when idle and brightens as you move the knob.
* **5DOF Navigation:** Smooth X, Y, Z translation + Pitch and Roll.
* **Browser Config Page:** Set sensor orientation and button mapping from a page the mouse serves itself. No recompile.
* **No-Disassembly Updates:** Enter the bootloader with a button combo or a serial command instead of the board's BOOT button.

> [!IMPORTANT]
> **Hardware Limitation:** Spin/Twist rotation is **not functional** due to the physics of the current sensor setup. For best results, configure your 3DConnexion driver to use either **Pan/Zoom** or **Rotation** mode—not both simultaneously. Use the programmable buttons to toggle between these modes on-the-fly.

---

## 🛠️ Hardware Support

This firmware is designed for the **Adafruit QT Py RP2040**, but will possibly work on other RP2040 boards with possible minor pin changes.

| Component | Pin (Default) | Notes |
| :--- | :--- | :--- |
| **Sensor** | **TLx493D** | Auto-detects on `Wire1` (Stemma) or `Wire` (Solder). |
| **Buttons** | A0, A1, A2, A3 | Mappable to any HID button 1-32 (defaults 13, 14, 15, 16). |
| **NeoPixel** | GPIO 4 | Addressable RGB Strip (WS2812). |
| **Simple LED** | GPIO 3 | Standard 2-leg LED (PWM brightness). |

> **Note:** The firmware drives **GPIO 3 and GPIO 4 simultaneously**. You can connect your LED to either pin depending on your build, and change the behavior in `UserConfig.h`.

---
## 🚀 Building & Flashing

See [ADVANCED-INSTRUCTIONS.md](ADVANCED-INSTRUCTIONS.md) for the `arduino-cli` setup and compile command.

### Updating without opening the case

Once this firmware is installed, any of these brings up the `RPI-RP2` drive to copy a new `.uf2` onto:

* Hold **the lowest- and highest-numbered buttons** together while plugging the mouse in (with the default mapping, the buttons on A0 and A3). Let go once `RPI-RP2` appears: if they are still held when the new firmware starts, it goes straight back to the bootloader.
* Click **Reboot to bootloader** on the config page (or send `bootloader` over serial).
* Run `stty -f /dev/cu.usbmodemXXXX 1200` (macOS) or `stty -F /dev/ttyACMX 1200` (Linux).

The board's BOOT + RESET buttons are only needed for the very first flash.

---

## 🧭 Config Page

Hold **the lowest-numbered button** while plugging the mouse in (with the default mapping, the one on A0; after remapping, whichever you saved with the lowest number). The LEDs flash blue twice, and a read-only drive named `ADASPACE` appears with `CONFIG.HTM` on it. Open that file in **Chrome or Edge**, click **Connect**, and pick the mouse's serial port.

| Setting | Values | What it does |
| :--- | :--- | :--- |
| **Sensor rotation** | 0°, 90°, 180°, 270° | Corrects a sensor mounted sideways. Applies to both movement and rotation. |
| **Invert X / Y** | on / off | Corrects a mirrored (upside-down) sensor. |
| **Switch 1-4** | 1-32 | Which HID button each physical switch (A0-A3) sends. The saved numbering also decides which switches the plug-in combos use. |

**Apply** takes effect immediately; **Save to device** keeps it after unplugging. **Live view** draws an arrow showing the movement being sent, which makes it easy to find the right rotation. Saved settings survive firmware updates; **Restore defaults** returns to the values in `UserConfig.h`.

The page talks to the mouse over its USB serial port, which is present in normal mode too, so a saved copy of `CONFIG.HTM` works without config mode. The drive is just a convenient place to get the page.

---

## ⚙️ Configuration (`UserConfig.h`)

You can tweak the feel of your SpaceMouse without touching the complex code.

```cpp
// --- SENSOR SETTINGS ---
// Increase if movement feels too slow. Default: 150.0
#define CONFIG_TRANS_SCALE     150.0  

// --- LED CONFIGURATION ---
// 0 = Static (Solid Color)
// 1 = Breathing (Pulse)
// 2 = Reactive (Dim resting color, Brightens on movement)
#define LED_MODE            2

// Choose your preferred color (RGB 0-255)
#define LED_COLOR_R         0
#define LED_COLOR_G         255
#define LED_COLOR_B         255
```




## 🤝 Credits & Acknowledgments

This project stands on the shoulders of giants in the DIY community.
* **[Salim Benbouzid](https://www.youtube.com/watch?v=iHBgNGnTiK4&t=323s):** For setting the seed and inspiring the DIY SpaceMouse revolution with his original video.
* **[AndunHH](https://github.com/AndunHH/spacemouse):** For his excellent work on the software side, proving the concept of using the drivers for navigation.
* **[LeoSpaceLab](https://makerworld.com/en/models/2029436-spacemouse-diy-no-springs-fusion360-magnets#profileId-2188468):** For refining the mechanical design that brought us all here (watch the [build video](https://www.youtube.com/watch?v=LWE_vR1oFRo)).

**Developed with ❤️ for the maker community by Axiom3d (aka Uzzo)**

> ⚠️ **Disclaimer:** This project is intended for educational purposes only. The creators assume no liability for any damages or injuries — build and modify at your own risk.

This work is licensed under a **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0)**.
You are free to:
* **Share:** Copy and redistribute the material in any medium or format.
* **Adapt:** Remix, transform, and build upon the material.
Under the following terms:
* **Attribution:** You must give appropriate credit to the original authors.
* **NonCommercial:** You may not use the material for commercial purposes.
* **ShareAlike:** If you remix, transform, or build upon the material, you must distribute your contributions under the same license as the original.
