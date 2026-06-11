# Home Lamps Automation — Design Document

## 1. Overview

Automate **five USB lamps** in the same room so they can all be turned on or off together by a single voice command through Amazon Alexa. The five lamps split into two groups by how they're controlled:

- **Group A (button-press, 3 lamps):** color cube + 2 portraits. Each has a built-in power button on its control board. To toggle the lamp, we simulate a button press with a BC547 transistor bridge across the button pads (same approach proven in the [mushroom-light project](../mushroom_light_automation/DESIGN.md)).
- **Group B (USB-power, 2 lamps):** sunset lamp + light drawing board. These have no button — they turn on as soon as USB power arrives and stay on until power is removed. We control them by switching their +5 V USB rail with a low-side N-MOSFET driven from an ESP32 GPIO.

A single ESP32 handles all five lamps. A single **Sinric Pro Light device** exposes the whole rig as one entity to Alexa: `Alexa, turn on the lamps` turns all five on; `Alexa, turn off the lamps` turns all five off.

The whole rig runs from one powerbank (no wall outlet available where the lamps live).

## 2. Goals & Non-Goals

### Goals

- One voice command turns the entire room's 5 lamps on or off.
- Preserve each lamp's normal manual operation — physical buttons on Group A lamps still work; Group B lamps still light up if their USB cable is plugged into a normal source.
- Battery-powered (powerbank) so the rig isn't tied to an outlet.
- Survive ESP32 reboots and powerbank power-loss cleanly (state restored after reboot; everything boots to a known state after a cold start).

### Non-Goals

- Individual lamp control via voice. *(All 5 are addressed as one group. Adding per-lamp control later only requires registering more Sinric Pro devices — see §11.)*
- Brightness or color control. *(None of the 5 lamps expose these as separate buttons; out of scope.)*
- Local-only operation; this design depends on Wi-Fi + Sinric Pro cloud.
- Hard-cutting power to Group A lamps. *(Same rationale as the mushroom project — see §5.1.)*

## 3. Hardware Inventory

| Component | Spec | Qty | Role |
|---|---|---|---|
| Powerbank | 100 W, 60,000 mAh, USB 5 V output | 1 | Energy source |
| ESP32 dev board | Any classic ESP32 with Wi-Fi | 1 | Controller |
| USB hub | 5+ ports, USB-A, power-pass-through OK | 1 | Combines all loads onto one powerbank port (3 Group A + ESP32 + male→Dupont feed) |
| Group A lamps | 5 V USB, internal power button, on/off only | 3 | Color cube + 2 portraits |
| Group B lamps | 5 V USB, always-on when powered | 2 | Sunset lamp + light drawing board |
| BC547 NPN BJT | Vce ~45 V, Ic max 100 mA | 3 | Button bridge transistor (one per Group A lamp) |
| Logic-level N-channel MOSFET | e.g., IRLZ44N or 2N7000 — must turn fully on at Vgs = 3.3 V | 2 | USB +5 V switch (one per Group B lamp) |
| 1 kΩ resistors | — | 5 | Base resistor for BC547 (3) + gate series resistor for MOSFET (2) |
| 10 kΩ resistors | — | 2 | Gate pull-down for MOSFET (one per Group B switch) |
| USB male → Dupont cable | USB-A male, breaks out to +5 V / GND Dupont leads | 1 | Power feed: plugs into the hub, supplies the +5 V / GND junction ("rail") |
| USB female → Dupont cable | USB-A female receptacle, +5 V / GND Dupont leads | 2 | One per Group B lamp: lamp plugs into the receptacle; its GND leg is switched by the MOSFET |
| Wires, breadboard | — | — | Interconnect |

## 4. System Architecture

```
            ┌───────────────────────────────────────────┐
            │   Powerbank 5 V USB output (single port)  │
            └──────────────────┬────────────────────────┘
                               │ 5 V
                               ▼
                       ┌───────────────┐
                       │  USB hub      │   (combines load to stay above
                       │  (5+ ports)   │    powerbank's auto-shutoff)
                       └─┬─┬─┬─┬─┬─────┘
                         │ │ │ │ │
     ┌───────────────────┘ │ │ │ └──────────────┐
     │      ┌───────────────┘ │ └──────┐         │
     │      │      ┌───────────┘        │         │
     ▼      ▼      ▼                    ▼         ▼
  Group A Group A Group A            ESP32     USB male→Dupont
  lamp 1  lamp 2  lamp 3           (own USB    cable = +5V / GND
  (always (always (always           port —     junction ("rail")
   powered)powered)powered)         powered     ├─► Group B lamp 1 +5V
                                     via USB,    └─► Group B lamp 2 +5V
                                     NOT VIN)
                                                 (ESP32 needs NO +5V from
                                                  the rail — only a shared
                                                  GND wire; see §5.5)

       Group A button pads:                Group B GND returns through MOSFETs:
       ─────────────────────               ──────────────────────────────────
        SIG ──► BC547 collector             Lamp GND ──► MOSFET drain
        GND ──► BC547 emitter ──► GND       MOSFET source ──► GND rail
                BC547 base ◄─[1kΩ]─ GPIO    MOSFET gate ◄─[1kΩ]─ GPIO
                                                       └─[10kΩ]─ GND

   Shared GND: ESP32 GND ── explicit jumper ──► Dupont GND junction
               (= BC547 emitters + MOSFET sources). All references common.


   Control flow:
      "Alexa, turn on the lamps"
              │
              ▼
      Sinric Pro cloud ──► WebSocket ──► ESP32 (Wi-Fi)
                                         │
                ┌────────────────────────┼────────────────────────┐
                ▼                        ▼                        ▼
       For each Group A lamp:    For each Group B lamp:    Persist state to NVS
       pulse GPIO HIGH 250 ms,    drive GPIO HIGH
       BC547 shorts button pads,  MOSFET conducts,
       lamp toggles state         lamp powers on
```

## 5. Key Design Decisions

### 5.1 Do NOT cut power to Group A lamps
Same rationale as the mushroom project:

- ESP32 dominates the energy budget vs. Group A standby draw.
- Low-side cutting of Group A's GND creates a leakage path through the BC547 bridge to ESP32 GND, partially powering the lamp's MCU.
- Group A lamps retain their internal state across "off" periods, which we don't need to track here (just on/off) but is harmless.

So Group A lamps stay always-powered via the USB hub. We only simulate button presses.

### 5.2 Button simulation via BC547 low-side bridge (Group A)
Identical to the mushroom project — proven working:

- Per lamp: 1× BC547 + 1× 1 kΩ.
- ESP32 GPIO → 1 kΩ → base; collector to button SIG pad, emitter to button GND pad.
- Base drive: `Ib = (3.3 V − 0.7 V) / 1 kΩ ≈ 2.6 mA` → deep saturation.
- **Pulse width: 250 ms HIGH, 200 ms gap.** (80 ms was below the mushroom lamp's debounce threshold; 250 ms registers reliably. Tune in firmware if a specific lamp model differs.)

Before soldering wires to each lamp's button: identify SIG vs GND with a multimeter (the SIG pad reads ~3.3–5 V idle when the lamp is on and drops to 0 V when the button is pressed). Connect collector to SIG side, emitter to GND side. Reversed C/E gives bad saturation and the lamp won't respond.

### 5.3 USB power switching via low-side N-MOSFET (Group B)
Group B lamps have no button — power = on, no power = off. To control them we gate their +5 V supply with a switching transistor.

**Decision: low-side N-MOSFET, not high-side P-MOSFET.** Reasoning:

- Group B lamps are fully isolated (no signal lines back to the ESP32 or any other lamp), so floating their GND when "off" is harmless.
- Low-side switching needs **1 component per lamp** (a logic-level N-MOSFET driven directly by GPIO). High-side P-MOSFET would need an additional level-shift transistor per lamp.
- The MOSFET must be **logic-level** — gate fully turns on at Vgs = 3.3 V. Examples: IRLZ44N (47 A, TO-220, robust), 2N7000 (200 mA, TO-92, tiny — only OK if lamp draws < 200 mA), AO3400 (SMD, ~5 A).

Per-lamp circuit:

```
   +5V rail ──────────────────► Lamp +5V wire
   Lamp GND wire ──► Drain
                     Source ──► common GND rail
                     Gate   ◄─[1 kΩ]─ ESP32 GPIO
                            ◄─[10 kΩ]─ GND (pull-down)
```

- **1 kΩ in series with the gate** limits inrush current during the GPIO transition (the gate looks like a small capacitor; without the resistor, the transition would briefly draw tens of milliamps from the GPIO).
- **10 kΩ pull-down from gate to source (GND)** holds the gate at 0 V while the ESP32 boots, before `pinMode(OUTPUT)` is called. Without it, the gate is high-impedance and could float HIGH briefly, turning the lamp on at every boot.

### 5.4 GPIO selection

| Function | GPIO |
|---|---|
| Group A lamp 1 — color cube (power button) | 25 |
| Group A lamp 2 — portrait 1 (power button) | 26 |
| Group A lamp 3 — portrait 2 (power button) | 27 |
| Group B lamp 1 — sunset (MOSFET gate) | 32 |
| Group B lamp 2 — light drawing board (MOSFET gate) | 33 |

All 5 are regular outputs, not strapping pins. All must be initialized LOW in `setup()` **before** enabling output mode — this prevents:

- Spurious button presses on Group A during boot
- Spurious power-on flashes on Group B during boot (more critical here than on the mushroom project because a flash would visibly light the lamps for a fraction of a second).

### 5.5 Power distribution — single port via USB hub
**Everything draws from ONE powerbank port via a USB hub.** Same lesson as the mushroom project, scaled up:

- Powerbanks watch each USB port's current draw independently and shut a port off when sustained draw falls below ~50–100 mA.
- Per-port draw of a single Group A standby lamp (~10 mA) is well below that threshold.
- Combining all 5 lamps + ESP32 behind one hub keeps total draw at the powerbank port ≥ 90 mA even with everything "off" (just ESP32 + 3 Group A standby), and ~830 mA with everything on. (The hub is passive/pass-through — it has no per-downstream-port shutoff of its own, so the keep-alive logic only matters at the single powerbank port the hub plugs into.)

The hub's downstream ports carry: the 3 Group A lamps, the **ESP32 (via its own USB cable)**, and a **USB-A-male → Dupont cable** that breaks the hub's +5 V / GND out to the Group B switching circuit. Hence a **5+ port** hub.

#### ESP32 power: USB only, no VIN feed — but GND must be shared

The ESP32 is powered through **its own USB port** (5 V → onboard regulator → 3.3 V). It does **not** take +5 V from the Dupont rail, and you should not feed both at once — tying the rail's 5 V into `VIN` while USB is connected back-feeds two 5 V sources past the board's USB diode.

What the ESP32 **does** need is a **common ground** with the switching circuit. Both schemes are low-side: the BC547 emitters and the MOSFET sources reference the Dupont GND node, and the gate/base voltages the ESP32 drives are only meaningful relative to that node. Electrically, every ground already returns to the powerbank's single negative terminal through the hub — so it works with no extra wire — but relying on GND continuity through three stacked USB connectors (hub upstream → hub port → Dupont) is fragile once packaged. **Run one explicit GND jumper from an ESP32 GND pin to the Dupont GND junction.** That guarantees a solid shared reference for all five drivers.

### 5.6 Control surface — single Sinric Pro device
**One Sinric Pro Light device controls all 5 lamps as a group.** Reasoning:

- The user goal is "all lamps in the room on/off with one command" — matches a single-device model.
- Sinric Pro on/off callback → ESP32 toggles all 5 in sequence.
- Individual lamp control via Alexa can be added later without rewiring — just register 5 more Sinric devices and add per-device callbacks (see §11).

Sinric Pro device type: **SinricProSwitch** or **SinricProLight** (both work; Light gets nicer Alexa UI). One device, on/off only.

### 5.7 State persistence
A single boolean `all_on` is persisted in ESP32 NVS via the `Preferences` library. On every Sinric command, the firmware:

1. Updates `all_on`
2. Saves to NVS
3. Walks through all 5 lamps and applies the target state

On boot:

- If `esp_reset_reason()` indicates **power-loss** (POWERON/BROWNOUT), reset `all_on` to `false`. After full power loss, Group A lamps come up off and Group B lamps come up off (MOSFETs default off). Reality matches NVS.
- If it indicates a **soft reset** (Wi-Fi watchdog, OTA, etc.), trust the persisted `all_on` and re-apply it to all lamps. Group A lamps stayed in whatever state they were in (their power wasn't cut, but their button-press state isn't electronically observable — we just assume they match). Group B lamps lost no power either, but the MOSFETs went off when the ESP32 rebooted, so they need re-energizing.

> **Caveat for Group A on soft reset:** because we can't read back a Group A lamp's actual state, a soft reset might find them in a different physical state than NVS says. The user can manually press each lamp's button to resync, or trigger an explicit "Alexa, turn on/off" cycle.

## 6. Schematic

### 6.1 Top-level wiring

```
                              Powerbank 5V (single USB output)
                                          │
                                          ▼
                                ┌─────────────────────┐
                                │   USB Hub (5+ port)  │
                                └─┬─┬─┬─┬─┬───────────┘
                                  │ │ │ │ │
            ┌─────────────────────┘ │ │ │ └──────────────────────┐
            │     ┌──────────────────┘ │ └──────────┐            │
            │     │      ┌──────────────┘            │            │
            ▼     ▼      ▼                            ▼            ▼
        Group A Group A Group A                    ESP32    USB-A male→Dupont
        lamp 1  lamp 2  lamp 3                   (own USB     cable (+5V / GND)
        (always (always (always                   port)            │
         on)    on)     on)                          │      +5V leg feeds:
         │      │      │                             │       ├─► Group B lamp 1 +5V
         │      │      │                       (NO VIN wire   └─► Group B lamp 2 +5V
         │      │      │                        from rail)
         │      │      └─────► BC547 #3 collector
         │      └────────────► BC547 #2 collector
         └──────────────────► BC547 #1 collector
                               (one bridge per Group A button — see §6.2)

   GND junction (Dupont +5V/GND cable's GND leg) feeds:
    ├─► ESP32 GND          (explicit jumper — see §5.5)
    ├─► BC547 emitters (×3)
    ├─► Group B lamp 1 GND via MOSFET drain (see §6.3)
    └─► Group B lamp 2 GND via MOSFET drain (see §6.3)
```

> The "+5V / GND rail" is not a breadboard — it's the Dupont breakout of the **USB-A-male → Dupont** cable plugged into the hub. The ESP32 is powered separately through its own USB port; the only wire it shares with the rail is **GND**.

### 6.2 Group A — Button bridge (per lamp, ×3)

```
                       lamp's power-button pads (wires soldered on)
                                ┌──────────────┐
                                │   SIG pad    │
                                │   (idle 3.3- │
                                │    5V when   │
                                │    on, ~0V   │
                                │    when off) │
                                └──────┬───────┘
                                       │
                                       ▼
   ESP32 GPIO (25/26/27) ──[1 kΩ]──► Base ┐
                                         │   Q (BC547, NPN)
                                         ├── Collector ◄── (from SIG above)
                                         └── Emitter ──► GND rail
                                       ▲
                                       │
                                ┌──────┴───────┐
                                │   GND pad    │
                                │   (always 0V)│
                                └──────┬───────┘
                                       │
                                       ▼
                                    GND rail
```

When the GPIO pulses HIGH (250 ms), Q saturates and shorts SIG to GND — equivalent to a finger press. The lamp's MCU sees the transition and toggles the lamp on/off.

**Soldering note:** verify SIG vs GND with a multimeter on each lamp before soldering. With the lamp powered on, the pad that idles HIGH and drops to 0 V when you physically press the button is SIG; the other is GND.

### 6.3 Group B — USB +5 V switch (per lamp, ×2)

```
   Dupont +5V leg ────────────► Female receptacle +5V pin (uninterrupted)
                                  │
                                  └─► [Group B lamp plugs into the receptacle]

                                  Female receptacle GND pin
                                       │
                                       ▼
                                     Drain
                                     ┌──────┐
                                     │  Q   │   Logic-level N-MOSFET
                                     │      │   (IRLZ44N or 2N7000)
                                     └──┬───┘
                                        │ Source
                                        │
                                        ▼
                                     GND junction

   ESP32 GPIO (32/33) ──[1 kΩ]──┬── Gate
                                │
                                └─[10 kΩ]── GND junction
                                    (pull-down, keeps gate at 0V at boot)
```

When the GPIO is HIGH, the MOSFET conducts, pulling the lamp's GND through to the common GND — lamp turns on. When LOW, the MOSFET is off, the lamp's GND floats, lamp is dark.

**Cable handling (this build):** each Group B lamp plugs into a **USB-A female → Dupont** cable. The cable's +5 V leg comes straight from the Dupont rail (uninterrupted); its GND leg is the one broken by the MOSFET (receptacle GND → drain, source → common GND). Only +5 V and GND matter — D+/D- are unused. *(Alternative if you don't have the female cables: splice the lamp's own USB cable mid-way and route the bare +5 V / GND wires the same way — but the female-receptacle cable avoids cutting the lamp's cable at all.)*

## 7. Firmware Architecture

### 7.1 Modules

| Module | Responsibility |
|---|---|
| `wifi_manager` | Connect to Wi-Fi, reconnect on drop |
| `sinric_handler` | Register the single Light device, handle on/off callback |
| `lamp_control` | Per-lamp `apply(bool on)` — pulses BC547 base for Group A, sets MOSFET gate for Group B |
| `state_store` | Load/save `all_on` from NVS |

### 7.2 Lamp configuration table

```cpp
enum LampType { BUTTON, USB_SWITCH };

struct Lamp {
    const char* name;     // for logging
    LampType    type;
    uint8_t     gpio;
};

const Lamp LAMPS[] = {
    { "color_cube",       BUTTON,     25 },
    { "portrait_1",       BUTTON,     26 },
    { "portrait_2",       BUTTON,     27 },
    { "sunset",           USB_SWITCH, 32 },
    { "drawing_board",    USB_SWITCH, 33 },
};
const int LAMP_COUNT = sizeof(LAMPS) / sizeof(LAMPS[0]);
```

### 7.3 Pseudocode

```cpp
constexpr uint16_t PRESS_HOLD_MS = 250;
constexpr uint16_t PRESS_GAP_MS  = 200;

bool all_on;  // persisted

void applyButton(const Lamp& l) {
    // Pulse the BC547 base. Lamp's own MCU toggles its state.
    digitalWrite(l.gpio, HIGH);
    delay(PRESS_HOLD_MS);
    digitalWrite(l.gpio, LOW);
    delay(PRESS_GAP_MS);
}

void applyUsbSwitch(const Lamp& l, bool on) {
    // MOSFET gate: HIGH = lamp powered, LOW = lamp dark.
    digitalWrite(l.gpio, on ? HIGH : LOW);
}

void setAllLamps(bool on) {
    if (on == all_on) return;       // idempotent
    for (const Lamp& l : LAMPS) {
        if (l.type == BUTTON) {
            applyButton(l);          // toggles whatever its current state is
        } else {
            applyUsbSwitch(l, on);
        }
    }
    all_on = on;
    saveState();
}
```

### 7.4 Sinric Pro callback

```cpp
bool onPowerState(const String& deviceId, bool& state) {
    setAllLamps(state);
    return true;
}
```

### 7.5 Boot sequence

```cpp
void setup() {
    // 1. Pre-set every GPIO LOW before enabling output drivers
    for (const Lamp& l : LAMPS) {
        digitalWrite(l.gpio, LOW);
        pinMode(l.gpio, OUTPUT);
        digitalWrite(l.gpio, LOW);
    }

    Serial.begin(115200);
    state::load();              // restores all_on from NVS
    handleResetReason();        // power-loss → all_on = false
    setupWiFi();
    setupSinric();

    // After Wi-Fi is up, apply persisted state (re-energize MOSFETs;
    // for buttons we cannot know real state, but we assume NVS is correct).
    if (all_on) {
        for (const Lamp& l : LAMPS) {
            if (l.type == USB_SWITCH) {
                applyUsbSwitch(l, true);
            }
            // Skip Button lamps — they kept their state across soft reset
            // (their USB power didn't drop); a power-loss reset would have
            // set all_on = false earlier so we wouldn't reach this branch.
        }
    }
}
```

## 8. Energy Budget

Combined draw on the single shared powerbank port:

| State | ESP32 | 3× Group A | 2× Group B | Total |
|---|---|---|---|---|
| All lamps off | ~80 mA | ~30 mA (standby) | 0 mA (MOSFETs off) | **~110 mA** |
| All lamps on | ~80 mA | ~450 mA (~150 each) | ~300 mA (~150 each) | **~830 mA** |

Daily energy at typical 5 PM – 11 PM usage (5 h on, 19 h off):

```
   off:  110 mA × 19 h = 2.09 Ah
   on:   830 mA ×  5 h = 4.15 Ah
   ────────────────────────────
   total per day:       ~6.24 Ah
```

Powerbank usable capacity: ~44.4 Ah @ 5 V (60,000 mAh × 3.7 V × 0.85 efficiency / 5 V).
**Estimated autonomy: ~7 days per full powerbank charge.**

Combined draw is always ≥ ~110 mA → comfortably above any auto-shutoff threshold even in the off-state.

## 9. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Spurious actuation on ESP32 boot | Initialize all 5 GPIOs LOW before `pinMode(OUTPUT)`; verify on first power-up that no lamp lights up or clicks |
| Group A lamp doesn't respond to bridge | Collector/emitter swapped, or wrong button pad identified as SIG. Verify per §6.2 — manual short test should toggle the lamp |
| Group B lamp current exceeds MOSFET rating | Use IRLZ44N (47 A) by default; only use 2N7000 (200 mA) after verifying the specific lamp's draw |
| MOSFET gate floats at boot, lamp glitches on | 10 kΩ pull-down from gate to GND (§5.3) holds the gate at 0 V before firmware takes over |
| Powerbank port auto-shuts on low load | All 5 lamps + ESP32 share one powerbank port via a USB hub; combined draw ≥ 110 mA always (§5.5) |
| State desync between NVS and physical Group A lamps | Document the manual-resync workflow: user can either physically press the offending lamp's button or trigger another Sinric on/off cycle (idempotent setting + retoggle if needed) |
| Wi-Fi disconnect → loss of voice control | Sinric Pro library auto-reconnects; state survives in NVS |
| Lamp's MCU latch-up via button pad | Group A lamps stay always-powered (§5.1) — no leakage path through the bridge |

## 10. Bring-Up Plan

Do these in order on the bench before final installation.

1. **Identify SIG/GND pads** on each Group A lamp (multimeter) and solder tap wires.
2. **Identify +5V/GND wires** in each Group B lamp's USB cable (red = +5 V, black = GND on a standard USB cable; verify with multimeter against the lamp's own USB plug).
3. **Breadboard one Group A bridge first** (color cube). Verify it toggles via serial command `on_1` / `off_1`.
4. **Add the other two Group A bridges**, verify each one independently.
5. **Breadboard the two Group B MOSFET switches.** Verify each lamp turns on/off via serial command.
6. **Wire all 5 lamps + ESP32 through the USB hub on one powerbank port**, confirm everything still works for at least an hour with everything off (power-budget keep-alive sanity).
7. **Layer in Sinric Pro single Light device**, register in Alexa, test "Alexa, turn on/off the lamps".
8. **Test soft reset** — press ESP32 EN button while lamps are on; verify state restores correctly after reboot.
9. **Test power-loss reset** — fully disconnect powerbank, wait 10 s, reconnect; verify all lamps come up off and NVS state matches.
10. **Final packaging** — enclosure, cable routing, mount.

A separate `BRINGUP.md` (to be created later, modeled on the mushroom project's verification gates) will spell out the multimeter checks between each step.

## 11. Open Questions / Future Work

- **Individual lamp control via Alexa.** Currently a single Sinric Pro device groups all 5. Adding per-lamp control means: register 5 additional Sinric Pro devices (one per lamp), keep the group device for "all on/off", and route each device's callback to a per-lamp `applyButton()` / `applyUsbSwitch()` instead of the all-lamps loop. Zero hardware changes required.
- **Group A state inference.** Since we can't read back a button-press lamp's actual state, NVS may drift from reality if the user physically presses a lamp while the ESP32 is offline. Possible future improvements: monitor the SIG pad voltage continuously with another GPIO (would need a voltage divider to bring 5 V down to 3.3 V); or accept the limitation and document the manual-resync workflow.
- **Confirm Group B lamp current draw.** §3 assumes ~150 mA each. If either lamp draws > 200 mA, we'd want to use IRLZ44N (already the default recommendation) and verify the USB hub / cabling can sustain the higher current.
- **Sinric Pro account / device IDs** — confirm setup is done on the user side before writing firmware credentials.
