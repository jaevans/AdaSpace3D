# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Arduino firmware for a DIY SpaceMouse: an Adafruit QT Py RP2040 reading an Infineon TLx493D 3D magnetometer, presenting itself over USB as a 3Dconnexion "SpaceMouse Pro Wireless" so the stock 3Dconnexion drivers work. This is a personal fork (`origin` = jaevans/AdaSpace3D, `upstream` = Axiom3D-YT/AdaSpace3D); changes are not sent upstream, and the upstream Windows one-click flasher was removed. There is no CI. Firmware behaviour can only be checked on hardware; the host-side drive test below is the only automated check.

## Build

Arduino CLI with the `rp2040:rp2040` core (Earle Philhower's arduino-pico) and the libraries "Adafruit TinyUSB Library", "XENSIV 3D Magnetic Sensor TLx493D" and "Adafruit NeoPixel". `EEPROM` comes with the core. Setup commands are in `ADVANCED-INSTRUCTIONS.md`.

The sketch must be compiled from a folder named `AdaSpace3D` containing `AdaSpace3D.ino` and every `.h` (not `test/`). From that folder's parent:

```bash
arduino-cli compile --fqbn "rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb" \
  --build-property "build.vid=0x256f" \
  --build-property "build.pid=0xc631" \
  --build-property "build.usbvid=-DUSBD_VID=0x256f" \
  --build-property "build.usbpid=-DUSBD_PID=0xc631" \
  --build-property 'build.usb_product="SpaceMouse Pro Wireless"' \
  --build-property 'build.usb_manufacturer="3Dconnexion"' \
  --output-dir "./output" "./AdaSpace3D"
```

`usbstack=tinyusb` is required — the sketch uses `Adafruit_USBD_HID` and `Adafruit_USBD_MSC`. The VID/PID also live in `UserConfig.h` (`TinyUSBDevice.setID`) and must agree with the build properties.

`test/run.sh` (macOS only) compiles `test/virtual_drive_test.cpp` with the host compiler, builds the config-mode drive image from `VirtualDrive.h` + `ConfigPage.h`, mounts it read-only with `hdiutil`, and checks `CONFIG.HTM` byte-for-byte and the `ADASPACE` volume name. It includes a negative control (zeroed bytes-per-sector must fail to mount); a corrupted `0x55AA` signature still mounts on macOS, so that is not a usable control.

## Firmware architecture

- **Boot (`setup`)**: settings load first, then the button pins are read once, 10 ms after `INPUT_PULLUP`. The combos follow the saved mapping: "button 1" is the switch mapped to the lowest HID number and "button 4" the highest, with ties broken by wiring order so both always exist. Lowest+highest → `rp2040.rebootToBootloader()`; lowest alone → config mode. Escape hatches if a mapping confuses things: 1200-baud touch, `bootloader`/`defaults` serial commands (normal mode too), the board's BOOT button. Then HID begins, MSC begins (config mode only), and the device is detached/re-attached if already mounted — the core initialises TinyUSB (including the CDC serial port) before `setup()` runs, so interfaces added in `setup` need re-enumeration. HID must begin before MSC so its interface number is the same in both modes. Sensor probe: `Wire1` (Stemma, green) then `Wire` (solder, cyan); no sensor → `blinkError()` forever in normal mode, but in config mode the loop still runs so the page works.
- **HID protocol**: `spaceMouse_hid_report_desc` defines report 1 = translation, report 2 = rotation (int16 each), report 3 = 32-bit button bitmap. `updateButtons` rebuilds the bitmap from the pins and the active mapping every loop and resends until `sendReport` succeeds.
- **Axis mapping (`readAndSendMagnetometerData`)**: calibrate → `applyOrientation` (rotate 0/90/180/270, then invert X/Y) → deadzone → scale. The same X/Y deflection drives translation (`tx=-x`, `ty=-y`) and rotation (`rx=y`, `ry=x`); Rz is always 0 because the sensor cannot sense twist.
- **Runtime settings**: packed `Settings` struct at EEPROM address 0 (magic `ADA3`, `SETTINGS_VERSION`, zero-sum checksum). Invalid or missing → `UserConfig.h` `CONFIG_ORIENTATION` / `CONFIG_INVERT_*` / `CONFIG_BUTTONn_HID` defaults. Change the struct → bump `SETTINGS_VERSION`. `EEPROM.commit` runs with interrupts off, so HID pauses briefly on save.
- **Serial command protocol** (CDC, both modes, one reply line per command): `info`, `get`, `set <key> <value>`, `save`, `defaults`, `bootloader`, `stream on|off` (`xy <x> <y> <bits>` lines every 50 ms; bits = physical switches held, bit 0 = A0, sent even without a sensor). `writeLine` drops output that doesn't fit the TX FIFO, because `Adafruit_USBD_CDC::write` spins when full and would stall HID. The 1200-baud touch to enter the bootloader comes from Adafruit TinyUSB itself, not from this sketch.
- **Config page**: `ConfigPage.h` holds the page as a raw string (delimiter `ADAPAGE`); it uses Web Serial (Chrome/Edge only) and checks `proto=` against `PROTOCOL_VERSION`. Change the protocol → bump both. `VirtualDrive.h` (pure C++, no Arduino deps) generates a read-only FAT12 superfloppy (1.44 MB floppy geometry, no partition table) containing just `CONFIG.HTM`; MSC writes are rejected and the medium reports write-protected.
- **LEDs**: `updateHardwareLeds` drives the NeoPixel strip (GPIO 4) and a PWM LED (GPIO 3) together. `strip.show()` blocks, so `handleLeds` is rate-limited (500 ms / 50 ms) to avoid starving HID (commits `3ca2685`, `f11fab3`). Keep new LED work off the per-loop path.

## Doc/code drift to be aware of

The prose in `UserConfig.h` and `README.md` disagrees with the code in places; trust the code:

- README and the `UserConfig.h` comment call 150.0 the default/"golden" `CONFIG_TRANS_SCALE`; the shipped value is `100`.
- `UserConfig.h` describes `LED_MODE 2` as "Debug / Reactive (Status Colors + White Flash on Move)" and says colours are ignored in it; `handleLeds` actually scales `LED_COLOR_*` from dim (50/255) to full with movement.
- The `USB_VID`/`USB_PID` comment names `0x046d/0xc626` (SpaceNavigator); the values are `0x256f/0xc631`.
- The sketch redefines `PIN_NEOPIXEL` to 4 over the variant's 12 (the on-board pixel); the build warns about it. README documents GPIO 4 as the strip pin, so the override is what the hardware guide expects.

## License

CC BY-NC-SA 4.0 (non-commercial, share-alike).
