# AdaSpace3D - Build & Flash Instructions

---

## Prerequisites

Install **Arduino CLI** (v1.1.1 or later):

- macOS: `brew install arduino-cli`
- Linux / Windows: download a release from [arduino/arduino-cli releases](https://github.com/arduino/arduino-cli/releases) and put it on your PATH

---

## One-Time Setup

```bash
# Initialize Arduino CLI config
arduino-cli config init

# Add RP2040 board support URL
arduino-cli config set board_manager.additional_urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

# Update core index
arduino-cli core update-index

# Install RP2040 core
arduino-cli core install rp2040:rp2040

# Install required libraries
arduino-cli lib install "Adafruit TinyUSB Library"
arduino-cli lib install "XENSIV 3D Magnetic Sensor TLx493D"
arduino-cli lib install "Adafruit NeoPixel"
```

---

## Building the Firmware

### Step 1: Create a Clean Sketch Folder

Create a folder named `AdaSpace3D` and copy into it:
- `AdaSpace3D.ino`
- every `.h` file (`UserConfig.h`, `ConfigPage.h`, `VirtualDrive.h`)

Do not copy the `test/` folder.

### Step 2: Compile with Custom USB Descriptors

Navigate to the **parent directory** of your `AdaSpace3D` sketch folder, then run the appropriate command for your shell:

#### Linux / macOS

```bash
arduino-cli compile --fqbn "rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb" \
  --build-property "build.vid=0x256f" \
  --build-property "build.pid=0xc631" \
  --build-property "build.usbvid=-DUSBD_VID=0x256f" \
  --build-property "build.usbpid=-DUSBD_PID=0xc631" \
  --build-property 'build.usb_product="SpaceMouse Pro Wireless"' \
  --build-property 'build.usb_manufacturer="3Dconnexion"' \
  --output-dir "./output" \
  "./AdaSpace3D"
```

#### CMD (Windows Command Prompt)

```cmd
arduino-cli compile --fqbn "rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb" --build-property "build.vid=0x256f" --build-property "build.pid=0xc631" --build-property "build.usbvid=-DUSBD_VID=0x256f" --build-property "build.usbpid=-DUSBD_PID=0xc631" --build-property "build.usb_product=\"SpaceMouse Pro Wireless\"" --build-property "build.usb_manufacturer=\"3Dconnexion\"" --output-dir "./output" "./AdaSpace3D"
```

#### PowerShell

```powershell
arduino-cli compile --fqbn "rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb" --build-property "build.vid=0x256f" --build-property "build.pid=0xc631" --build-property "build.usbvid=-DUSBD_VID=0x256f" --build-property "build.usbpid=-DUSBD_PID=0xc631" --build-property "build.usb_product=`"SpaceMouse Pro Wireless`"" --build-property "build.usb_manufacturer=`"3Dconnexion`"" --output-dir "./output" "./AdaSpace3D"
```

This creates a `.uf2` file in the `./output` folder.

---

## Flashing the Firmware

### Step 1: Put the Mouse in Bootloader Mode

Any one of these makes a drive called `RPI-RP2` appear:

- **Hold the lowest- and highest-numbered buttons while plugging the mouse in** (A0 and A3 with the default mapping; after remapping, the ones you saved with the lowest and highest numbers). Release them once `RPI-RP2` appears, or the freshly flashed firmware will see them held and return to the bootloader.
- **Serial command:** connect to the mouse's serial port and send `bootloader`, or use the *Reboot to bootloader* button on the config page.
- **1200-baud touch:** `stty -f /dev/cu.usbmodemXXXX 1200` on macOS (`stty -F /dev/ttyACMX 1200` on Linux).
- **Board buttons (first flash, or if the firmware is broken):** hold BOOT, press and release RESET, release BOOT.

### Step 2: Copy the Firmware

Copy `AdaSpace3D.ino.uf2` from the `./output` folder to the `RPI-RP2` drive.

The device will automatically restart with the new firmware installed.

---

## Testing the Config Drive (macOS)

`test/run.sh` builds the config-mode drive image on your computer, mounts it read-only, and checks that `CONFIG.HTM` and the `ADASPACE` volume name come out exactly as the firmware serves them. It also checks that a deliberately corrupted image fails to mount, so a pass actually means something.

```bash
test/run.sh
```

---

## USB Descriptor Reference

These build properties make the device appear as a 3DConnexion SpaceMouse:

| Property | Value |
|----------|-------|
| VID (Vendor ID) | `0x256f` (3Dconnexion) |
| PID (Product ID) | `0xc631` (SpaceMouse Pro Wireless) |
| Product Name | SpaceMouse Pro Wireless |
| Manufacturer | 3Dconnexion |

> [!NOTE]
> The TinyUSB stack is required for HID functionality.

---

## Troubleshooting

### "Board not found" error
- Ensure you ran: `arduino-cli core install rp2040:rp2040`

### "Library not found" error
- Run the lib install commands from the [One-Time Setup](#one-time-setup) section

### "ConfigPage.h: No such file or directory"
- Copy every `.h` file into the sketch folder, not just `UserConfig.h`

### "Permission denied" when copying UF2
- Ensure the `RPI-RP2` drive is mounted and writable
- Try running your terminal as Administrator (Windows)

### Device not recognized by 3DConnexion driver
- Verify the USB VID/PID are correctly set in the compile command
- Check what VID/PID the device reports (System Information on macOS, Device Manager on Windows)
