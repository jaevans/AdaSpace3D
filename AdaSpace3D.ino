/*
 * AdaSpace3D - Unified Firmware (Golden Release)
 * * Features: Dual LED Drive, Reactive Lighting, Auto-Hardware Detect
 * * Safety:   Includes Sensor Watchdog to auto-reset frozen I2C lines
 */

#include "Adafruit_TinyUSB.h"
#include "TLx493D_inc.hpp"
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "UserConfig.h"
#include "VirtualDrive.h"
#include "ConfigPage.h"

// --- CONSTANTS ---
#define PIN_NEOPIXEL   4
#define PIN_SIMPLE     3
#define HANG_THRESHOLD 50              // consecutive identical readings before reset
#define PREVENTIVE_RESET_INTERVAL 0    // Set to 300000 (5 mins) if you want periodic resets

// Serial protocol revision; ConfigPage.h checks for "proto=2".
#define PROTOCOL_VERSION   2
// Bump when the Settings layout changes so old EEPROM contents are ignored.
#define SETTINGS_VERSION   1
#define SETTINGS_MAGIC     0x33414441UL   // "ADA3" as little-endian bytes
#define EEPROM_SIZE        256
#define STREAM_INTERVAL_MS 50

static_assert(CONFIG_ORIENTATION == 0 || CONFIG_ORIENTATION == 90 ||
              CONFIG_ORIENTATION == 180 || CONFIG_ORIENTATION == 270, "CONFIG_ORIENTATION must be 0, 90, 180 or 270");
static_assert(CONFIG_INVERT_X <= 1 && CONFIG_INVERT_Y <= 1, "CONFIG_INVERT_X/Y must be 0 or 1");
static_assert(CONFIG_BUTTON1_HID >= 1 && CONFIG_BUTTON1_HID <= 32 &&
              CONFIG_BUTTON2_HID >= 1 && CONFIG_BUTTON2_HID <= 32 &&
              CONFIG_BUTTON3_HID >= 1 && CONFIG_BUTTON3_HID <= 32 &&
              CONFIG_BUTTON4_HID >= 1 && CONFIG_BUTTON4_HID <= 32, "CONFIG_BUTTONn_HID must be 1-32");
static_assert(CONFIG_PAGE_LEN <= VDRIVE_MAX_FILE_BYTES, "config page does not fit the virtual drive");

// --- LED SETUP ---
Adafruit_NeoPixel strip(NUM_ADDRESSABLE_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- HARDWARE GLOBALS ---
const uint8_t physPins[] = {BUTTON1_PIN, BUTTON2_PIN, BUTTON3_PIN, BUTTON4_PIN};
uint32_t lastButtonBitmap = 0;
bool buttonReportPending = false;

// --- RUNTIME SETTINGS (persisted in EEPROM) ---
struct __attribute__((packed)) Settings {
  uint32_t magic;
  uint8_t  version;
  uint16_t orientation;
  uint8_t  invertX;
  uint8_t  invertY;
  uint8_t  buttons[4];
  uint8_t  checksum;   // makes the byte sum of the struct zero
};
// EEPROM.get/put silently do nothing past EEPROM_SIZE.
static_assert(sizeof(Settings) <= EEPROM_SIZE, "Settings no longer fit in EEPROM");

Settings settings;
bool configMode = false;

// --- SERIAL / STREAM STATE ---
char lineBuf[65];
uint8_t lineLen = 0;
bool lineOverflow = false;
bool streaming = false;
unsigned long lastStreamMs = 0;
double streamX = 0.0, streamY = 0.0;
bool streamHasData = false;

// --- DATA STRUCTURES ---
struct MagCalibration {
  double x_neutral = 0.0, y_neutral = 0.0, z_neutral = 0.0;
  bool calibrated = false;
} magCal;

struct SensorWatchdog {
  double last_x = 0.0, last_y = 0.0, last_z = 0.0;
  int sameValueCount = 0;
  unsigned long lastResetTime = 0;
  unsigned long lastPreventiveReset = 0;
} watchdog;

using namespace ifx::tlx493d;

TLx493D_A1B6 magCable(Wire1, TLx493D_IIC_ADDR_A0_e);
TLx493D_A1B6 magSolder(Wire, TLx493D_IIC_ADDR_A0_e);
TLx493D_A1B6* activeSensor = nullptr;

// HID Report Descriptor
static const uint8_t spaceMouse_hid_report_desc[] = {
  0x05, 0x01, 0x09, 0x08, 0xA1, 0x01, 0xA1, 0x00, 0x85, 0x01, 0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x36, 0x00, 0x80, 0x46, 0xFF, 0x7F,
  0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x75, 0x10, 0x95, 0x03, 0x81, 0x02, 0xC0,
  0xA1, 0x00, 0x85, 0x02, 0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x36, 0x00, 0x80, 0x46, 0xFF, 0x7F,
  0x09, 0x33, 0x09, 0x34, 0x09, 0x35, 0x75, 0x10, 0x95, 0x03, 0x81, 0x02, 0xC0,
  0xA1, 0x00, 0x85, 0x03, 0x05, 0x09, 0x19, 0x01, 0x29, 0x20, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x20, 0x81, 0x02, 0xC0,
  0xC0
};

Adafruit_USBD_HID usb_hid;
Adafruit_USBD_MSC usb_msc;
#define CALIBRATION_SAMPLES 50

// --- SETTINGS ---

uint8_t settingsSum(const Settings &s) {
  const uint8_t *p = (const uint8_t *)&s;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(Settings); i++) sum += p[i];
  return sum;
}

bool validOrientation(long v) { return v == 0 || v == 90 || v == 180 || v == 270; }
bool validHidButton(long v)   { return v >= 1 && v <= 32; }

bool settingsInRange(const Settings &s) {
  if (!validOrientation(s.orientation) || s.invertX > 1 || s.invertY > 1) return false;
  for (uint8_t i = 0; i < 4; i++) if (!validHidButton(s.buttons[i])) return false;
  return true;
}

void defaultSettings(Settings &s) {
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;
  s.orientation = CONFIG_ORIENTATION;
  s.invertX = CONFIG_INVERT_X;
  s.invertY = CONFIG_INVERT_Y;
  s.buttons[0] = CONFIG_BUTTON1_HID;
  s.buttons[1] = CONFIG_BUTTON2_HID;
  s.buttons[2] = CONFIG_BUTTON3_HID;
  s.buttons[3] = CONFIG_BUTTON4_HID;
  s.checksum = 0;
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  Settings stored;
  EEPROM.get(0, stored);
  if (stored.magic == SETTINGS_MAGIC && stored.version == SETTINGS_VERSION &&
      settingsSum(stored) == 0 && settingsInRange(stored)) {
    settings = stored;
  } else {
    defaultSettings(settings);
  }
}

bool saveSettings() {
  settings.checksum = 0;
  settings.checksum = (uint8_t)(0 - settingsSum(settings));
  EEPROM.put(0, settings);
  return EEPROM.commit();  // erases/programs with interrupts off; HID pauses briefly
}

// --- CONFIG-MODE DRIVE ---

int32_t mscRead(uint32_t lba, void *buffer, uint32_t bufsize) {
  uint8_t *out = (uint8_t *)buffer;
  for (uint32_t done = 0; done < bufsize; done += VDRIVE_SECTOR_SIZE) {
    vdrive_read_sector(lba++, out + done, CONFIG_PAGE, CONFIG_PAGE_LEN);
  }
  return bufsize;
}

int32_t mscWrite(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  (void)lba; (void)buffer; (void)bufsize;
  return -1;
}

bool mscWritable() { return false; }

// --- LED LOGIC ---

void updateHardwareLeds(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t c = strip.Color(r, g, b);
  strip.fill(c);
  strip.show();

  int brightness = (r * 77 + g * 150 + b * 29) >> 8; 
  brightness = (brightness * LED_BRIGHTNESS) / 255;
  analogWrite(PIN_SIMPLE, brightness);
}

void blinkError() {
  while(1) {
    updateHardwareLeds(255, 0, 0); delay(100);
    updateHardwareLeds(0, 0, 0);   delay(100);
  }
}

void handleLeds(double totalMove) {
  if (LED_MODE == 0) { // STATIC
    // Rate-limit: static color doesn't need updating every cycle
    static unsigned long lastStaticUpdate = 0;
    if (millis() - lastStaticUpdate < 500) return; // Only refresh every 500ms
    lastStaticUpdate = millis();
    updateHardwareLeds(LED_COLOR_R, LED_COLOR_G, LED_COLOR_B);
  }
  else if (LED_MODE == 1) { // BREATHING
    float val = (exp(sin(millis()/2000.0*PI)) - 0.36787944)*108.0;
    uint8_t r = (LED_COLOR_R * (int)val) / 255;
    uint8_t g = (LED_COLOR_G * (int)val) / 255;
    uint8_t b = (LED_COLOR_B * (int)val) / 255;
    updateHardwareLeds(r, g, b);
  }
  else if (LED_MODE == 2) { // REACTIVE
      // Rate-limit LED updates to prevent strip.show() from blocking HID reports
      static unsigned long lastLedUpdate = 0;
      if (millis() - lastLedUpdate < 50) return; // Only update LEDs every 50ms
      lastLedUpdate = millis();
      
      int minScale = 50;  
      int maxScale = 255; 
      int currentScale = minScale;

      if (totalMove > CONFIG_DEADZONE) {
         int addedIntensity = (int)(totalMove * 30.0);
         currentScale = constrain(minScale + addedIntensity, minScale, maxScale);
      }
      
      uint8_t r = (LED_COLOR_R * currentScale) / 255;
      uint8_t g = (LED_COLOR_G * currentScale) / 255;
      uint8_t b = (LED_COLOR_B * currentScale) / 255;
      updateHardwareLeds(r, g, b);
  }
}

void resetMagnetometer() {
  // Briefly flash Red to indicate reset
  updateHardwareLeds(255, 0, 0);
  
  activeSensor->end();
  delay(50);
  
  // Restart the correct Wire interface
  if (activeSensor == &magCable) {
      Wire1.end(); delay(50); Wire1.begin(); 
  } else {
      Wire.end(); delay(50); Wire.begin();
  }
  delay(50);
  
  if (activeSensor->begin()) {
    watchdog.sameValueCount = 0;
    watchdog.lastResetTime = millis();
    // Return to normal color
    updateHardwareLeds(LED_COLOR_R, LED_COLOR_G, LED_COLOR_B);
  }
}

void setup() {
  pinMode(PIN_SIMPLE, OUTPUT);
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  updateHardwareLeds(255, 0, 0); // Boot Red

  loadSettings();

  // Plug-in combos follow the user's numbering: "button 1" is the switch
  // mapped to the lowest HID number, "button 4" the highest, ties broken by
  // wiring order so both always exist. Invalid settings fall back to defaults
  // (wiring order), and 1200-baud / the BOOT button remain as escape hatches.
  for(uint8_t i = 0; i < 4; i++) pinMode(physPins[i], INPUT_PULLUP);
  delay(10);
  bool held[4];
  for(uint8_t i = 0; i < 4; i++) held[i] = (digitalRead(physPins[i]) == LOW);
  uint8_t first = 0, last = 0;
  for(uint8_t i = 1; i < 4; i++) {
    if (settings.buttons[i] < settings.buttons[first]) first = i;
    if (settings.buttons[i] >= settings.buttons[last]) last = i;
  }
  if (held[first] && held[last]) rp2040.rebootToBootloader();
  configMode = held[first];
  for(uint8_t i = 0; i < 4; i++) if (i != first && held[i]) configMode = false;

  TinyUSBDevice.setID(USB_VID, USB_PID);
  // HID must begin before MSC so its interface number is the same in both modes.
  usb_hid.setReportDescriptor(spaceMouse_hid_report_desc, sizeof(spaceMouse_hid_report_desc));
  usb_hid.setPollInterval(2);
  usb_hid.begin();

  if (configMode) {
    usb_msc.setID("AdaSpace", "Config Page", "1.0");
    usb_msc.setCapacity(VDRIVE_SECTOR_COUNT, VDRIVE_SECTOR_SIZE);
    usb_msc.setReadWriteCallback(mscRead, mscWrite, nullptr);
    usb_msc.setWritableCallback(mscWritable);
    usb_msc.setUnitReady(true);
    usb_msc.begin();
  }

  // The core starts USB before setup(); re-enumerate if the host already
  // saw the device without these interfaces.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  while(!TinyUSBDevice.mounted()) delay(100);
  
  pinMode(MAG_POWER_PIN, OUTPUT);
  digitalWrite(MAG_POWER_PIN, HIGH);
  delay(10); 

  Wire1.begin(); Wire1.setClock(400000);
  if (magCable.begin()) {
     activeSensor = &magCable;
     updateHardwareLeds(0, 255, 0); delay(500); // Green for Cable
  } 
  else {
     Wire.begin(); Wire.setClock(400000);
     if (magSolder.begin()) {
        activeSensor = &magSolder;
        updateHardwareLeds(0, 255, 255); delay(500); // Cyan for Solder
     } else if (!configMode) {
        blinkError();
     }
  }

  if (configMode) {
    for (int i = 0; i < 2; i++) {
      updateHardwareLeds(0, 0, 255); delay(150);
      updateHardwareLeds(0, 0, 0);   delay(150);
    }
  }

  watchdog.lastPreventiveReset = millis();
  // In config mode a missing sensor must not stop the loop, which serves
  // the page's commands.
  if (activeSensor) calibrateMagnetometer();
}

void loop() {
  serviceSerial();
  updateButtons();

  if (activeSensor) {
    readAndSendMagnetometerData();
  }
  serviceStream();
  delay(2);
}

void calibrateMagnetometer() {
  double sumX = 0, sumY = 0, sumZ = 0;
  int valid = 0;
  
  updateHardwareLeds(0, 0, 0); delay(100);
  updateHardwareLeds(255, 255, 255); 
  
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    double x, y, z;
    if(activeSensor->getMagneticField(&x, &y, &z)) {
      sumX += x; sumY += y; sumZ += z;
      valid++;
    }
    delay(10);
  }

  if(valid > 0) {
    magCal.x_neutral = sumX / valid;
    magCal.y_neutral = sumY / valid;
    magCal.z_neutral = sumZ / valid;
    magCal.calibrated = true;
    updateHardwareLeds(0, 0, 0); delay(200);
  } else {
    for(int i=0; i<5; i++) {
       updateHardwareLeds(255, 0, 0); delay(50);
       updateHardwareLeds(0, 0, 0); delay(50);
    }
    calibrateMagnetometer();
  }
}

void readAndSendMagnetometerData() {
  double x, y, z;
  
  // Preventive Reset Check
  if(PREVENTIVE_RESET_INTERVAL > 0 && millis() - watchdog.lastPreventiveReset > PREVENTIVE_RESET_INTERVAL) {
    resetMagnetometer();
    watchdog.lastPreventiveReset = millis();
    return;
  }

  if(activeSensor->getMagneticField(&x, &y, &z)) {
      if(isfinite(x) && isfinite(y) && isfinite(z)) {
          
          // --- WATCHDOG LOGIC ---
          // If values are IDENTICAL to last read, sensor might be frozen
          if(x == watchdog.last_x && y == watchdog.last_y && z == watchdog.last_z) {
            watchdog.sameValueCount++;
            if(watchdog.sameValueCount >= HANG_THRESHOLD) {
              resetMagnetometer();
              return;
            }
          } else {
            watchdog.sameValueCount = 0;
            watchdog.last_x = x;
            watchdog.last_y = y;
            watchdog.last_z = z;
          }

          // Calibration
          if(magCal.calibrated) {
            x -= magCal.x_neutral;
            y -= magCal.y_neutral;
            z -= magCal.z_neutral;
          }

          applyOrientation(x, y);
          streamX = x;
          streamY = y;
          streamHasData = true;

          double totalMove = abs(x) + abs(y) + abs(z);
          handleLeds(totalMove);

          if(abs(x) < CONFIG_DEADZONE) x = 0.0;
          if(abs(y) < CONFIG_DEADZONE) y = 0.0;
          if(abs(z) < CONFIG_ZOOM_DEADZONE) z = 0.0;

          int16_t tx = (int16_t)(-x * CONFIG_TRANS_SCALE);
          int16_t ty = (int16_t)(-y * CONFIG_TRANS_SCALE);
          int16_t tz = (int16_t)(z * CONFIG_ZOOM_SCALE);
          int16_t rx = (int16_t)(y * CONFIG_ROT_SCALE);
          int16_t ry = (int16_t)(x * CONFIG_ROT_SCALE);
          
          send_tx_rx_reports(tx, ty, tz, rx, ry, 0);
      }
  } else {
      // If we get an error reading, report 0
      send_tx_rx_reports(0, 0, 0, 0, 0, 0);
  }
}

void applyOrientation(double &x, double &y) {
  double ox = x, oy = y;
  switch (settings.orientation) {
    case 90:  x = -oy; y =  ox; break;
    case 180: x = -ox; y = -oy; break;
    case 270: x =  oy; y = -ox; break;
    default: break;
  }
  if (settings.invertX) x = -x;
  if (settings.invertY) y = -y;
}

// Rebuilt from the physical pins every loop so a mapping change while a
// button is held cannot leave a stale bit set.
void updateButtons() {
  uint32_t bitmap = 0;
  for(uint8_t i = 0; i < 4; i++) {
    if (digitalRead(physPins[i]) == LOW) bitmap |= 1UL << (settings.buttons[i] - 1);
  }
  if (bitmap == lastButtonBitmap && !buttonReportPending) return;
  lastButtonBitmap = bitmap;

  if (!TinyUSBDevice.mounted() || !usb_hid.ready()) {
    buttonReportPending = true;
    return;
  }
  uint8_t report[4] = {(uint8_t)bitmap, (uint8_t)(bitmap >> 8), (uint8_t)(bitmap >> 16), (uint8_t)(bitmap >> 24)};
  buttonReportPending = !usb_hid.sendReport(3, report, 4);
}

// --- SERIAL COMMANDS ---

// Writes only when the whole line fits: Adafruit_USBD_CDC::write spins while
// the TX FIFO is full, which would stall HID if the host stops reading.
bool writeLine(const char *line) {
  size_t len = strlen(line);
  if (!Serial || Serial.availableForWrite() < (int)(len + 1)) return false;
  Serial.write(line, len);
  Serial.write('\n');
  return true;
}

void replyErr(const char *reason) {
  char buf[64];
  snprintf(buf, sizeof(buf), "err %s", reason);
  writeLine(buf);
}

bool parseNumber(const char *s, long &out) {
  if (!s || !*s || strlen(s) > 3) return false;
  for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return false;
  out = atol(s);
  return true;
}

void handleSet(char *key, char *value) {
  long v;
  if (!key || !parseNumber(value, v)) { replyErr("usage: set <key> <number>"); return; }

  if (strcmp(key, "orientation") == 0) {
    if (!validOrientation(v)) { replyErr("orientation must be 0, 90, 180 or 270"); return; }
    settings.orientation = (uint16_t)v;
  } else if (strcmp(key, "invert_x") == 0 || strcmp(key, "invert_y") == 0) {
    if (v > 1) { replyErr("invert must be 0 or 1"); return; }
    if (key[7] == 'x') settings.invertX = (uint8_t)v; else settings.invertY = (uint8_t)v;
  } else if (strncmp(key, "button", 6) == 0 && key[6] >= '1' && key[6] <= '4' && key[7] == '\0') {
    if (!validHidButton(v)) { replyErr("button must be 1-32"); return; }
    settings.buttons[key[6] - '1'] = (uint8_t)v;
  } else {
    replyErr("unknown key");
    return;
  }
  writeLine("ok");
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  char *arg1 = strtok(nullptr, " ");
  char *arg2 = strtok(nullptr, " ");
  if (!cmd) return;

  char buf[128];
  if (strcmp(cmd, "info") == 0) {
    snprintf(buf, sizeof(buf), "ok adaspace3d proto=%d", PROTOCOL_VERSION);
    writeLine(buf);
  } else if (strcmp(cmd, "get") == 0) {
    snprintf(buf, sizeof(buf),
             "ok orientation=%u invert_x=%u invert_y=%u button1=%u button2=%u button3=%u button4=%u",
             settings.orientation, settings.invertX, settings.invertY,
             settings.buttons[0], settings.buttons[1], settings.buttons[2], settings.buttons[3]);
    writeLine(buf);
  } else if (strcmp(cmd, "set") == 0) {
    handleSet(arg1, arg2);
  } else if (strcmp(cmd, "save") == 0) {
    if (saveSettings()) writeLine("ok"); else replyErr("save failed");
  } else if (strcmp(cmd, "defaults") == 0) {
    defaultSettings(settings);
    writeLine("ok");
  } else if (strcmp(cmd, "bootloader") == 0) {
    writeLine("ok");
    Serial.flush();
    // flush() only queues; give the host time to read the reply.
    delay(100);
    rp2040.rebootToBootloader();
  } else if (strcmp(cmd, "stream") == 0 && arg1 && strcmp(arg1, "on") == 0) {
    streaming = true;
    writeLine("ok");
  } else if (strcmp(cmd, "stream") == 0 && arg1 && strcmp(arg1, "off") == 0) {
    streaming = false;
    writeLine("ok");
  } else {
    replyErr("unknown command");
  }
}

void serviceSerial() {
  if (!Serial.dtr()) streaming = false;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (lineLen > 0 && lineBuf[lineLen - 1] == '\r') lineLen--;
      lineBuf[lineLen] = '\0';
      if (lineOverflow) replyErr("line too long");
      else handleCommand(lineBuf);
      lineLen = 0;
      lineOverflow = false;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineOverflow = true;
    }
  }
}

// Runs without sensor data too (x/y stay 0), so the page can still show
// which physical switch is pressed when no sensor was found.
void serviceStream() {
  if (!streaming) return;
  if (millis() - lastStreamMs < STREAM_INTERVAL_MS) return;
  lastStreamMs = millis();
  uint8_t pressed = 0;
  for(uint8_t i = 0; i < 4; i++) if (digitalRead(physPins[i]) == LOW) pressed |= 1 << i;
  char buf[48], xs[16], ys[16];
  formatCenti(xs, sizeof(xs), streamHasData ? streamX : 0.0);
  formatCenti(ys, sizeof(ys), streamHasData ? streamY : 0.0);
  snprintf(buf, sizeof(buf), "xy %s %s %u", xs, ys, pressed);
  writeLine(buf);  // dropped if it does not fit
}

void formatCenti(char *out, size_t size, double v) {
  long c = lround(v * 100.0);
  unsigned long a = (unsigned long)labs(c);
  snprintf(out, size, "%s%lu.%02lu", c < 0 ? "-" : "", a / 100, a % 100);
}

void send_tx_rx_reports(int16_t tx, int16_t ty, int16_t tz, int16_t rx, int16_t ry, int16_t rz) {
  if (!TinyUSBDevice.mounted() || !usb_hid.ready()) return;

  uint8_t tx_report[6] = {(uint8_t)tx, (uint8_t)(tx>>8), (uint8_t)ty, (uint8_t)(ty>>8), (uint8_t)tz, (uint8_t)(tz>>8)};
  usb_hid.sendReport(1, tx_report, 6);
  
  delayMicroseconds(500);
  
  uint8_t rx_report[6] = {(uint8_t)rx, (uint8_t)(rx>>8), (uint8_t)ry, (uint8_t)(ry>>8), (uint8_t)rz, (uint8_t)(rz>>8)};
  usb_hid.sendReport(2, rx_report, 6);
}
