<img src="https://alternativebits.com/esp32/logo.png" alt="ESP32 Logo"/>

The **ESP32 Bootloader** is a firmware for the **TTGO VGA32 v1.4** (or any ESP32) that turns the ESP32 into an emulator loader (or any other software) via SD card.

The idea is simple: instead of needing a computer to flash a different emulator onto the ESP32 every time, you simply insert an SD card with the desired firmware, power up the device, and it does everything automatically — checks if the firmware is new, flashes it if necessary, and starts the emulator.

The bootloader occupies the `factory` partition of the ESP32. Emulators are flashed to the `ota_0` partition. Next time you power up with the same firmware, it goes directly to the emulator without going through the bootloader. If you swap the card, it loads the new firmware. Pretty much like Multicore does.

> ⚠️ **WARNING: This project is in ULTRA SUPER BETA stage.** Use at your own risk, bugs are expected, things will break, and everything can change at any time. You have been warned! 😄

## Table of Contents

* [1. How does it work?](#1-how-does-it-work)

* [2. How to flash the bootloader?](#2-how-to-flash-the-bootloader)

* [3. Where to find the emulators?](#3-where-to-find-the-emulators)

  * [3.1 How to extract the correct firmware.bin](#31-how-to-extract-the-correct-firmwarebin)

* [4. Which emulators work?](#4-which-emulators-work)

* [5. Known issues](#5-known-issues)

* [6. Compatibility with Arduino IDE emulators](#6-compatibility-with-emulators-compiled-with-arduino-ide--arduino-framework)

* [7. ULTRA SUPER BETA](#7-ultra-super-beta)

* [8. Credits](#8-credits)

* [9. If you liked it](#9-if-you-liked-it-you-know-what-to-do)

***

## 1. How does it work?

### Boot flow

```
Power on ESP32
    │
    ▼
Display splash screen (ESP32 BOOTLOADER)
    │
    ▼
Check SD card
    ├── Not found → restart in 5s
    └── Found
            │
            ▼
        Read version.txt
            │
            ├── Version matches stored → boot directly to emulator ✅
            └── Different version → flash firmware.bin → boot to emulator ✅
```

### SD card structure

| File           | Description                                                            |
| -------------- | ---------------------------------------------------------------------- |
| `firmware.bin` | The emulator firmware                                                  |
| `version.txt`  | A simple text with the firmware name/version (e.g.: `ESPectrum_1.4.5`) |

### About FabGL and PS2Controller

The bootloader uses the **FabGL** library to display the splash screen via VGA. FabGL normally initializes the PS2 controller (keyboard) using the **ULP** (Ultra Low Power coprocessor) of the ESP32 — which caused conflicts with emulators that also need the PS2/ULP.

The solution was to locally modify the `ps2controller.cpp` of FabGL so that `begin()` does not initialize the ULP. With this, the keyboard of the emulators works normally after boot (God willing!).

### About firmware.bin

**Do not use the web flasher** **`.bin`** **directly!** For **ESPectrum**, use the `.upg` file directly — just rename it to `firmware.bin`. For **MSPX** and **CPC**, you need to extract the app from the merged `.bin` (see section 4.1).

## 2. How to flash the bootloader?

**Option 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Option 2 — Compile yourself:**

```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 3. Where to find the emulators?

* **ESPectrum:** [zxespectrum.speccy.org/flash](https://zxespectrum.speccy.org/flash/) — get the `.upg` file · [Direct download](https://alternativebits.com/esp32/ESPectrum.zip)

* **CPC:** [Direct download](https://alternativebits.com/esp32/CPCESP.zip)

* **MSPX:** Available only to Eremus sponsors. Link coming soon.

* **VIC20:** [Direct download](https://alternativebits.com/esp32/VIC20.zip)

## 3.1. How to extract the correct firmware.bin

### For MSPX and CPC (offset 0x40000)

```python
python3 -c "
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
app = data[0x40000:]
with open('firmware.bin', 'wb') as f:
    f.write(app)
print(f'firmware.bin: {len(app)//1024} KB')"
```

### To find the correct offset for any .bin

```python
python3 -c "
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
print(f'Total size: {len(data)} bytes ({len(data)//1024} KB)')
for offset in [0x0000, 0x1000, 0x8000, 0xe000, 0x10000, 0x40000, 0x90000, 0xa0000]:
    if offset < len(data):
        print(f'offset 0x{offset:05X}: 0x{data[offset]:02X}')"
```

Look for offsets returning `0xE9`. The **first** is the bootloader (skip). The **second** is the app (use this).

## 4. Which emulators work?

| Emulator                             | Repository                                                    | Status    |
| ------------------------------------ | ------------------------------------------------------------- | --------- |
| **ESPectrum** (ZX Spectrum 48K/128K) | [EremusOne/ESPectrum](https://github.com/EremusOne/ESPectrum) | ✅ Working |
| **CPC** (Amstrad CPC)                | EremusOne/CPCEsp                                              | ✅ Working |
| **MSPX** (MSX)                       | EremusOne/MSPX                                                | ✅ Working |
| **VIC20** (VIC20)                    | fdivitto/FabGL                                                | ✅ Working |

## 5. Known issues

* ⚠️ **PS2 Keyboard** — inconsistent behavior. If keyboard doesn't respond after boot, power off and on again.

* ⚠️ **PocketTRS** — not compatible yet due to WiFi conflicts and partition differences.

* ⚠️ **First flash after erase** — some emulators save settings in NVS. After erase flash, settings are lost.

* ⚠️ **ESPectrum self-update** — no longer works due to partition changes. Use the bootloader to update instead.

***

## 6. ⚠️ Compatibility with emulators compiled with Arduino IDE / Arduino Framework

Emulators compiled with the **Arduino IDE** or with the **Arduino framework in PlatformIO** automatically write the `otadata` pointing to themselves during boot. This causes the ESP32 to skip the bootloader on the next power-up.

To fix this, add the following block at the **very beginning of** **`setup()`** in the emulator, before anything else:

```cpp
#include "esp_ota_ops.h"
#include "esp_partition.h"

void setup() {
    // Ensures the bootloader runs on the next reset
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (otadata) {
        esp_partition_erase_range(otadata, 0, otadata->size);
    }

    // ... rest of original setup
}
```

> **Emulators compiled with ESP-IDF or PlatformIO (espidf framework)** do not need this modification and work normally with the bootloader.

***

## 7. ULTRA SUPER BETA

**It can crash · The API may change · It has bugs · But it works!** — most of the time.

## 8. Credits

* **[EremusOne](https://github.com/EremusOne)** — for ESPectrum, CPCEsp and MSPX

* **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — for the FabGL library

## 9. If you liked it, you know what to do!

Consider a donation through **[this link](https://github.com/sponsors/fg1998)**. I will spend all donated money on BEER 🍺

If you want to hire me as a freelancer for WEB or embedded projects, get in touch!

🇧🇷 Se você for brasileiro e quiser doar qualquer valor via PIX use a chave <fg1998@gmail.com>

*by Fernando Garcia —* *[fg1998](https://github.com/fg1998)*

*find me —* *<fg1998@gmail.com>*
