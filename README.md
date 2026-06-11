# Home Lamps Automation — Firmware

Voice-controlled automation for **five 5 V USB lamps** in one room, built on an ESP32 and integrated with Alexa through **Sinric Pro**. A single voice command turns the whole rig on or off: *"Alexa, turn on the lamps"*. See [DESIGN.md](DESIGN.md) for the full architecture, schematics, energy budget, and rationale. This README focuses on the firmware: what it does, how to set up the toolchain, how to build/flash it, and how to validate it on the bench.

The five lamps split into two control groups (full detail in [DESIGN.md §1](DESIGN.md)):

- **Group A — button-press (3 lamps):** color cube + 2 portraits. Each has a built-in power button; a BC547 transistor bridge across the button pads simulates a finger press.
- **Group B — USB-power (2 lamps):** sunset lamp + light drawing board. No button — they light up as soon as USB power arrives. A low-side N-MOSFET switches their +5 V ground return.

The firmware is C++ on the **Arduino framework**, built with **PlatformIO**, and uses the official [**Sinric Pro Arduino SDK**](https://github.com/sinricpro/esp8266-esp32-sdk) for the Alexa integration.

---

## 1. Behavior

| Event | What firmware does |
|---|---|
| **Turn on** (Alexa or serial `on`) | For each Group A lamp: pulse its BC547 base (250 ms) to toggle it on. For each Group B lamp: drive its MOSFET gate HIGH. Persist `all_on = true`. |
| **Turn off** (Alexa or serial `off`) | For each Group A lamp: pulse its BC547 base to toggle it off. For each Group B lamp: drive its MOSFET gate LOW. Persist `all_on = false`. |
| **Per-lamp test** (serial `on_1`…`on_5` / `off_1`…`off_5`) | Actuate a single lamp by index — bench bring-up only, not exposed to Alexa. |
| **Power loss** (powerbank disconnected/empty) | On next boot, detected via `esp_reset_reason()`; firmware resets persisted state to `all_on = false` to match reality (Group A lamps boot off; Group B MOSFETs default off). |
| **Soft reset** (Wi-Fi blip, watchdog, OTA) | Persisted `all_on` from NVS is trusted. Group B MOSFETs are re-energized to match it; Group A lamps are assumed unchanged (their USB power never dropped). |

### The on/off model is a group, not per-lamp

A **single Sinric Pro device** represents all five lamps. Alexa sees one switch; the firmware fans the command out to all five. Per-lamp voice control is a documented future extension that needs no hardware change — see [DESIGN.md §11](DESIGN.md).

### Idempotency and Group A drift

`setAllLamps(on)` is idempotent on the persisted flag: if the rig already thinks it's on, a second "on" is a no-op (Group A lamps are *toggle* buttons, so blindly re-pulsing them would turn them **off**). Because a button-press lamp's real state can't be read back, the ESP32's idea of Group A state can drift if someone presses a lamp physically while the ESP32 is offline. Resync by toggling once via Alexa/serial, or pressing the lamp's own button. See [DESIGN.md §5.7](DESIGN.md).

---

## 2. Project structure

```
home_lamps_automation/
├── DESIGN.md                 # Hardware design, schematics, energy budget
├── README.md                 # This file
├── platformio.ini            # PlatformIO build config (board, libs, monitor)
├── .gitignore
├── src/
│   └── main.cpp              # All firmware logic
├── include/
│   ├── config_example.h      # Template for Wi-Fi + Sinric credentials
│   └── config.h              # YOUR credentials (gitignored, created by setup)
└── scripts/
    └── setup.sh              # One-shot environment helper (bash)
```

### Boot flow (`setup()` in `main.cpp`)

```
setup()
  ├─ initGpios()        -> drive each pin LOW, then pinMode(OUTPUT), then LOW  (no boot glitches)
  ├─ loadState()        -> all_on from NVS (Preferences)
  ├─ isColdStart()      -> esp_reset_reason() POWERON/BROWNOUT → all_on = false
  ├─ if !coldStart && all_on:
  │     reapplySwitches()-> re-energize Group B MOSFETs                        (Group A untouched)
  ├─ setupWiFi()        -> station connect (blocks until WL_CONNECTED)
  └─ setupSinric()      -> register the single switch device + onPowerState callback

loop()
  ├─ SinricPro.handle() -> services the Sinric Pro WebSocket
  └─ handleSerial()     -> bench commands (on / off / on_N / state / reset)
```

---

## 3. Prerequisites

### 3.1 Tools

| Tool | Why | Where |
|---|---|---|
| **VS Code** | IDE | https://code.visualstudio.com/ |
| **PlatformIO IDE** (VS Code extension) | ESP32 toolchain, build, flash, serial monitor, library management | Install from VS Code Extensions: search "PlatformIO IDE" |
| **C/C++** (Microsoft, VS Code extension) | IntelliSense, code navigation | Usually auto-installed with PlatformIO |
| **USB-UART driver** | Lets the OS see the ESP32's USB-to-serial chip | See §3.2 |

The Sinric Pro SDK and its dependencies (ArduinoJson, WebSockets) are declared in `platformio.ini` and fetched automatically on the first build — nothing to install by hand.

> **Running in WSL.** The toolchain runs inside your WSL distro (Ubuntu/Debian assumed). If you don't use the VS Code extension, the PlatformIO **CLI** alone is enough: `pip install --user platformio` (add `~/.local/bin` to `PATH`). Either way, the ESP32's USB port has to be bridged from Windows into WSL — see §3.3.

### 3.2 USB-UART driver and serial access

Most ESP32 dev boards expose their UART through a USB-to-serial chip — identify yours from the markings on the board:

- **CP2102 / CP2104** (Silicon Labs) — common on NodeMCU-32S, ESP32 DevKit-V1.
- **CH340 / CH341 / CH9102** (WCH) — common on cheap clones.

The drivers ship in the mainline Linux kernel, so **no driver install is needed inside WSL**. Once the device is attached to WSL (§3.3), it appears as:

- `/dev/ttyUSB0` — CH340 / CH341
- `/dev/ttyACM0` — CP210x, or CH9102 on recent kernels

Confirm with `ls /dev/tty{USB,ACM}* 2>/dev/null` or `dmesg | tail` after attaching. PlatformIO usually auto-detects the port; pass `--upload-port` / `--monitor-port` to override.

One one-time step lets your user open the port without `sudo`:

```bash
sudo usermod -aG dialout $USER   # 'dialout' on Debian/Ubuntu
```

The group change does **not** take effect by just reopening the terminal on WSL — apply it via the VM restart in §3.3.

### 3.3 WSL — bridging USB from Windows

WSL2 doesn't see USB devices by default; the Windows host owns the USB stack. Use [**usbipd-win**](https://github.com/dorssel/usbipd-win) to forward the ESP32 into WSL. **These few commands are the only ones that run on the Windows side** — everything else in this README runs inside WSL.

#### One-time setup

1. **Install usbipd-win** from an elevated **Windows PowerShell**:

   ```powershell
   winget install --source winget --interactive --exact dorssel.usbipd-win
   ```

   If winget fails with a Microsoft Store / certificate error, grab the MSI from https://github.com/dorssel/usbipd-win/releases/latest. Reopen PowerShell after install so `usbipd` is on PATH.

2. **Identify and bind the ESP32.** In Windows PowerShell:

   ```powershell
   usbipd list
   # Find your chip's VID:PID — CP2102 (10c4:ea60), CH340 (1a86:7523), CH9102 (1a86:55d4)
   # Note its BUSID (e.g. 3-3), then bind it (admin PowerShell, persists across reboots):
   usbipd bind --busid 3-3
   ```

#### Per-session attach

After every Windows boot or `wsl --shutdown`, re-attach from Windows PowerShell (no admin needed):

```powershell
usbipd attach --wsl --busid 3-3
```

Then, inside WSL, confirm the device enumerated:

```bash
ls /dev/tty{USB,ACM}* 2>/dev/null
```

#### Applying the `dialout` group change

Reopening the WSL terminal isn't enough — WSL keeps old credentials in its init process. Restart the whole VM from Windows PowerShell:

```powershell
wsl --shutdown
```

Re-run `usbipd attach --wsl --busid 3-3`, open a fresh WSL shell, and `groups | grep dialout` should now list `dialout`.

### 3.4 Sinric Pro account

1. Sign up at https://sinric.pro/.
2. From the dashboard, create one new **Switch** (or **Light**) device to represent the whole rig.
3. Note the **App Key**, **App Secret**, and **Device ID** — they go in `include/config.h`.
4. In the Alexa app: enable the **Sinric Pro** skill, then *Devices → + → Add Device → Other → Discover devices*.

---

## 4. Setup

From a WSL terminal in this folder:

```bash
chmod +x scripts/setup.sh    # only needed the first time
./scripts/setup.sh
```

The script copies `include/config_example.h` to `include/config.h` (if missing), checks that `pio` is on PATH, and verifies you're in the `dialout` group. Then open `include/config.h` and fill in your values:

```c
#define WIFI_SSID         "your-wifi-ssid"
#define WIFI_PASS         "your-wifi-password"
#define SINRIC_APP_KEY    "from-sinric-pro-dashboard"
#define SINRIC_APP_SECRET "from-sinric-pro-dashboard"
#define SINRIC_DEVICE_ID  "from-sinric-pro-dashboard"
```

`config.h` is git-ignored so credentials don't leak. The ESP32 (classic) is **2.4 GHz only** — make sure the SSID is a 2.4 GHz network.

---

## 5. Build & flash

### Via VS Code (PlatformIO IDE)

1. Open this folder in VS Code (`File → Open Folder…`).
2. Wait for PlatformIO to initialize and fetch libraries (status bar at the bottom).
3. Click the **✓ (Build)** icon in the PlatformIO bar — or `Ctrl+Alt+B`.
4. Make sure the ESP32 is attached to WSL (§3.3).
5. Click the **→ (Upload)** icon — or `Ctrl+Alt+U`.
6. Click the **🔌 (Serial Monitor)** icon — or `Ctrl+Alt+S`. Baud is set to 115200 by `platformio.ini`.

### Via CLI

```bash
pio run                  # build (downloads SinricPro + deps on first run)
pio run -t upload        # build + flash
pio device monitor       # serial monitor at 115200
```

If the port isn't auto-detected, pass it explicitly:

```bash
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0
```

On the first successful boot the serial monitor prints the banner with the reset reason and restored state.

---

## 6. Wiring (recap)

Refer to [DESIGN.md §6 (Schematics)](DESIGN.md) for the full diagrams. Quick reference — all five GPIOs are regular outputs (no strapping pins):

| Lamp | Group | ESP32 GPIO | → | Through | → | To |
|---|---|---|---|---|---|---|
| color cube | A | 25 | → | 1 kΩ | → | Base of BC547 #1; C/E across button **SIG/GND** pads |
| portrait 1 | A | 26 | → | 1 kΩ | → | Base of BC547 #2; C/E across button **SIG/GND** pads |
| portrait 2 | A | 27 | → | 1 kΩ | → | Base of BC547 #3; C/E across button **SIG/GND** pads |
| sunset | B | 32 | → | 1 kΩ (+ 10 kΩ gate→GND pull-down) | → | Gate of MOSFET #1; drain to female-receptacle **GND** pin, source to common GND |
| drawing board | B | 33 | → | 1 kΩ (+ 10 kΩ gate→GND pull-down) | → | Gate of MOSFET #2; drain to female-receptacle **GND** pin, source to common GND |

**Power delivery — important:** all five lamps **and** the ESP32 draw from the **same** powerbank port through a **USB hub** (5+ ports; not separate powerbank ports). Powerbanks auto-shut off a port when its draw falls below ~50–100 mA, and the lamps' standby draw alone is too low. Combining everything behind the hub keeps that single port above ~110 mA even with all lamps off. See [DESIGN.md §5.5](DESIGN.md).

- **Group B +5 V / GND comes from a `USB-A-male → Dupont` cable** plugged into the hub — that Dupont breakout *is* the "+5 V / GND rail" (there's no breadboard). Each Group B lamp plugs into a `USB-A-female → Dupont` cable; its +5 V passes straight through and only its **GND leg** is switched by the MOSFET.
- **The ESP32 is powered through its own USB port — do _not_ wire its `VIN` to the rail.** Feeding both USB and `VIN` back-feeds two 5 V sources. The one wire the ESP32 must share with the rail is **GND**: run an explicit jumper from an ESP32 `GND` pin to the Dupont GND junction (the common reference for all BC547 emitters and MOSFET sources). Low-side switching won't work reliably without it.

**Before soldering Group A:** with the lamp powered, identify which button pad idles HIGH and drops to ~0 V when pressed — that's **SIG** (→ collector); the other is **GND** (→ emitter). Reversed C/E gives poor saturation and the lamp won't respond.

**Group B gate pull-down is not optional:** the 10 kΩ from gate to GND holds the MOSFET off while the ESP32 boots, before the GPIO is configured — without it the lamp can flash on at every power-up. See [DESIGN.md §5.3](DESIGN.md).

---

## 7. Validation

### 7.1 Serial commands (bench bring-up)

With the serial monitor open at 115200 baud, the firmware accepts these newline-terminated commands:

| Command | Effect |
|---|---|
| `on` | Turn all 5 lamps on (Group A toggled on, Group B MOSFETs HIGH) |
| `off` | Turn all 5 lamps off |
| `on_1` … `on_5` | Actuate a single lamp on (index follows the lamp table in §2) |
| `off_1` … `off_5` | Actuate a single lamp off (Group A is a toggle, so the verb only matters for Group B) |
| `state` | Print `all_on` plus Wi-Fi / Sinric connection state |
| `reset` | Clear persisted NVS state (use after manually power-cycling a lamp) |

### 7.2 Validation checklist

Mirrors the bring-up plan in [DESIGN.md §10](DESIGN.md). Work top to bottom:

1. **Boot sanity** — flash firmware, open the serial monitor. The boot banner prints the reset reason and restored state. **No lamp lights or clicks during boot** (confirms GPIO pre-init worked).
2. **Group A one at a time** — `on_1`. The color cube toggles. Repeat for `on_2`, `on_3`. If a lamp doesn't respond, suspect swapped collector/emitter or a misidentified SIG pad (§6).
3. **Group B one at a time** — `on_4`, then `off_4`; the sunset lamp powers on and off cleanly. Repeat `on_5` / `off_5` for the drawing board. Watch for a boot-time flash (missing/weak gate pull-down).
4. **Group command** — `on` brings all five up; `off` takes all five down.
5. **Idempotency** — with the rig on, send `on` again. **Group A lamps must stay on** (the firmware skips re-pulsing). If they flip off, the idempotent guard isn't firing.
6. **Cold-start handling** — fully disconnect the powerbank, wait 10 s, reconnect. Verify the banner reports a power-loss reset, persisted state is `all_on = false`, and all lamps are observably off.
7. **Soft reset preservation** — turn the rig on, then press only the ESP32's `EN`/`RST` button (lamps stay powered). Verify the banner reports a soft reset, `all_on` stays `true`, and **Group B lamps re-light** (MOSFETs re-energized) while Group A lamps were never interrupted.
8. **Wi-Fi + Sinric** — the monitor shows `[wifi] connected` and `[sinric] connected`; the Sinric Pro dashboard shows the device **online**.
9. **Alexa** — *"Alexa, turn on the lamps"* → all five on; *"Alexa, turn off the lamps"* → all five off.

---

## 8. Troubleshooting

### 8.1 Hardware / firmware

| Symptom | Likely cause | Fix |
|---|---|---|
| A lamp twitches/flashes during ESP32 boot | GPIO floated HIGH before being driven LOW | Group A: `initGpios()` already drives LOW before `pinMode(OUTPUT)` — check pins aren't swapped with strapping pins (avoid GPIO 0, 2, 5, 12, 15). Group B: the 10 kΩ gate pull-down (§6) is missing or open |
| Group A lamp doesn't respond to `on_N` | Emitter/collector swapped, or SIG/GND pad misidentified | Re-check with a multimeter (§6); a BC547 in reverse barely conducts |
| Group A lamp ends in the wrong state | Someone pressed its physical button while ESP32 was offline → state drift | `reset` over serial, then `on`/`off` to resync; or press the lamp's button once |
| Sending `on` twice turns Group A **off** | Idempotent guard not honored — buttons are toggles | Verify `setAllLamps` returns early when `on == allOn` (`main.cpp`) |
| Group B lamp never powers | MOSFET not a logic-level part, or gate/drain/source swapped | Use a logic-level FET (IRLZ44N / AO3400); confirm Vgs fully on at 3.3 V (DESIGN.md §5.3) |
| Whole rig dies after long idle | Powerbank shut the shared port off on low draw | Ensure **all 5 lamps + ESP32 share one port via the USB hub** (§6) — combined draw stays ≥ ~110 mA |
| Wi-Fi won't connect | SSID/pass typo, or 5 GHz-only network | ESP32 classic is 2.4 GHz only — check the router band and `include/config.h` |
| Lamp press too short to register | Lamp MCU debounce longer than the pulse | Increase `PRESS_HOLD_MS` in `src/main.cpp` (default 250 ms; some lamps need 300–400 ms) |

### 8.2 Sinric Pro / toolchain

| Symptom | Likely cause | Fix |
|---|---|---|
| Build fails resolving `SinricPro.h` | Libraries not fetched yet | Run `pio run` once with internet access; PlatformIO installs the deps in `platformio.ini` |
| Sinric device shows **offline** | Wrong app key/secret/device ID, or Wi-Fi down | Re-copy all three from the dashboard into `include/config.h`; check `state` output over serial |
| Alexa says "device isn't responding" | Device not discovered yet | Alexa app → *Devices → + → Add Device → Other → Discover devices* |
| `/dev/ttyUSB*` / `/dev/ttyACM*` never appears in WSL | Device not attached, or WSL didn't enumerate it | Re-run `usbipd attach --wsl --busid <id>` from Windows (§3.3); check `dmesg | tail`; if stale, `wsl --shutdown` then re-attach |
| `Permission denied` opening the port | Not in the `dialout` group | `sudo usermod -aG dialout $USER`, then `wsl --shutdown` and re-attach (§3.3) — reopening the terminal alone won't apply it |
| `usbipd: command not recognized` (Windows) | PowerShell started before install; PATH stale | Reopen PowerShell after installing usbipd-win (§3.3) |
| Upload can't open / reset the port | Wrong port, or another program holds it | Close any open serial monitor; pass `--upload-port`; some boards need holding **BOOT** during upload |

---

## 9. Implementation notes

- **GPIO init order matters.** `initGpios()` calls `digitalWrite(pin, LOW)` *before* `pinMode(pin, OUTPUT)` (then LOW again), so the output latch is low before the driver is enabled — no Group A click or Group B flash during boot. The Group B 10 kΩ gate pull-down covers the window before `setup()` runs at all (DESIGN.md §5.4).
- **State persistence** uses the Arduino `Preferences` (NVS) library. Namespace `lamps`, single boolean key `on`.
- **Power-loss detection** uses `esp_reset_reason()`: only `ESP_RST_POWERON` and `ESP_RST_BROWNOUT` reset `all_on = false`; all other reset causes trust NVS.
- **Sinric Pro integration** is the official SDK: one `SinricProSwitch` bound to `SINRIC_DEVICE_ID`, an `onPowerState` callback that calls `setAllLamps`, and `SinricPro.handle()` pumped from `loop()`. The SDK auto-reconnects the WebSocket, so the device returns to **online** on its own after a Wi-Fi blip.
- **Press timing.** `pressButton` holds the BC547 base HIGH for `PRESS_HOLD_MS` (250 ms) then waits `PRESS_GAP_MS` (200 ms). These are blocking `delay()`s — worst case ~1.35 s for all three Group A lamps — which is fine because Sinric requests are infrequent and `SinricPro.handle()` resumes right after. Bump `PRESS_HOLD_MS` if a specific lamp needs a longer press.
- **Idempotent group set.** `setAllLamps(on)` returns early when the target equals `allOn` — essential because Group A buttons are toggles, so re-pulsing an already-on lamp would turn it **off** (DESIGN.md §5.7).
- **Re-energizing Group B on soft reset.** MOSFET gates go LOW whenever the ESP32 reboots, so on a soft reset with `all_on = true`, `setup()` calls `reapplySwitches()` to re-drive the Group B gates HIGH. Group A is left alone — its USB power never dropped.
