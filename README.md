# Improved Fencing Equipment Tester

**Open source weapon and wire tester for fencing — field-tested at the European Championships.**

This device allows armourers and club officials to test all fencing equipment quickly and reliably: body cords, épées, foils, lamés, guards, and cable reels. It gives clear, colour-coded results based on FIE regulations — no guesswork, no external tools needed.
<img width="702" height="589" alt="TesterFront" src="https://github.com/user-attachments/assets/07527161-fcef-42ca-a199-ef3feea3df67" />

Unlike cheap continuity testers, this device **measures actual resistance values** and checks for **micro-breaks** — intermittent faults that pass a simple continuity check but cause unreliable scoring on the piste. All results are compared against FIE regulation thresholds and reported with colour-coded feedback.

Built and maintained by a member of the **FIE SEMI Commission** and **EFC SEMI Commission** (the technology commissions of the International Fencing Federation and European Fencing Confederation), and significantly improved after real-world use at the **2024/2025 European Championships in Genova**. Later used in World Cups and Grand Prix:

<img width="250"  alt="TunisTester" src="https://github.com/user-attachments/assets/96af60b4-7b8d-4ef0-b69f-6bfa2e771abd" />

> ⚠️ This is the active development repository. The original version is at [WeaponWireTester](https://github.com/pietwauters/WeaponWireTester) — kept for reference and documentation.

---

## Why This Tester Exists

Commercial fencing testers are expensive and often unavailable to smaller clubs and federations. This device is designed to be:

- **Affordable** — built on an ESP32 with off-the-shelf components
- **Accurate** — measurements calibrated against FIE resistance thresholds
- **Intuitive** — colour-coded LED feedback, no menus or buttons beyond power on/off
- **Reliable** — revised after direct armourer feedback at a major international competition

---

## What's New Since Genova

This repository is a significant revision of the original [WeaponWireTester](https://github.com/pietwauters/WeaponWireTester), based on direct feedback from professional armourers at the 2024/2025 European Championships.

Key improvements:

- **Cable Reel Testing Mode** — a new dedicated mode for testing cable reels, which was a recurring need in armoury conditions at Genova
- **Improved automatic mode switching** — the device detects what is connected and switches mode automatically wherever possible, reducing operator error
- **Pro version with OLED display** — an optional variant that shows exact resistance values on a small OLED display, for armourers who need the numbers and not just a colour verdict
- **Resistance measurement, not just continuity** — unlike most inexpensive testers, this device measures actual resistance and actively hunts for micro-breaks: intermittent faults that a simple pass/fail continuity check will miss entirely

---

## What It Tests

| Test | Weapon/Item |
|------|------------|
| Body cord continuity | All weapons |
| Weapon circuit resistance | Épée, Foil |
| Full path resistance (socket → weapon → tip) | Épée, Foil |
| Lamé conductivity | Foil/Sabre |
| Guard resistance | Épée, Foil, Sabre |
| Cable reel continuity | All |

All results are displayed using colour-coded feedback on a 5×5 LED matrix:

| Colour | Meaning |
|--------|---------|
| 🟢 Green | FIE-compliant |
| 🟡 Yellow | Works, but close to the limit — repair recommended |
| 🟠 Orange | Works, but does not comply with FIE regulations |
| 🔴 Red | Not working |

---

## Device Overview

The tester has two connection faces:

- **Bottom face** — three 4 mm sockets (A, B, C) for the body cord plug connecting to the cable reel
- **Top face** — three 4 mm sockets (A, B, C) plus one 3 mm socket for the body cord plug connecting to the weapon

Other features:
- Power button (on/off)
- M5 cap nut on the left side — used as a reference contact point for certain tests
- USB-C charging port (bottom face); blue LED indicates fully charged
- Rechargeable 3.7 V Li-ion battery

---

## Operating Modes

The tester uses **button-less mode switching**: modes are selected by making specific connection combinations between sockets, not by pressing buttons. This was a deliberate design choice to make the device foolproof in armoury conditions. To switch between any two modes, you return to Body Cord Tester mode first.

### Body Cord Tester Mode (default on power-up)

The starting mode for all tests. A yellow/orange snake animation plays during initialisation, followed by a blinking dot:

- **Green blinking dot** — device is calibrated
- **Red blinking dot** — device is using default calibration (see build manual)

Connect the body cord at both ends (bottom and top sockets). The tester runs in two steps:

1. Measures resistance of each wire individually. Blue lines indicate short circuits or wrong connections.
2. Switches to fast mode to detect micro-breaks — flex and stress the connections to reveal intermittent faults.

Result colours for each wire:

| Display | Resistance | Status |
|---------|-----------|--------|
| Solid green lines | < 1 Ω | FIE-compliant ✅ |
| Solid yellow lines | 1–3 Ω | Close to limit ⚠️ |
| Solid orange lines | 3–10 Ω | Not FIE-compliant ❌ |
| Broken red lines | > 10 Ω | Connection likely open ❌ |

When all three connections pass (all green), the blinking dot turns **blue** — the cord is ready to use for subsequent weapon tests, and its resistance is stored and subtracted from further measurements.

---

### Épée Mode

Connect the épée to a body cord plugged into the bottom connection. Press the tip to enter Épée Mode (an "E" appears on the display).

- **Press tip against a non-conductive surface** — measures épée circuit resistance only
- **Press tip against the M5 cap nut** — measures full path from socket to tip

| Display | Resistance | Status |
|---------|-----------|--------|
| Green "E" | < 2 Ω | FIE-compliant ✅ |
| Yellow "E" | 2–4 Ω | Close to limit ⚠️ |
| Orange "E" | > 4 Ω | Not FIE-compliant ❌ |
| White "E" | > 20 Ω | Épée not working ❌ |
| Blue lines | — | Short circuit detected |

To return to Body Cord Tester mode: short-circuit any bottom socket with socket B or C on the top face.

---

### Foil Mode

Connect the foil to a body cord plugged into the bottom connection. The tester enters Foil Mode automatically. A green "F" is displayed.

- **Press tip against a non-conductive surface** — measures foil circuit resistance (white square while pressed)
- **Press tip against the M5 cap nut** — measures full path from socket to tip

Foil circuit resistance:

| Display | Resistance | Status |
|---------|-----------|--------|
| Green "F" | < 2 Ω | FIE-compliant ✅ |
| Yellow "F" | 2–4 Ω | Close to limit ⚠️ |
| Orange "F" | > 4 Ω | Not FIE-compliant ❌ |
| Blue lines | — | Short circuit detected |

Full path (socket → foil → tip):

| Display | Resistance | Status |
|---------|-----------|--------|
| Green square | < 1 Ω | FIE-compliant ✅ |
| Yellow square | 1–2 Ω | Close to limit ⚠️ |
| Orange square | > 2 Ω | Not FIE-compliant ❌ |

To return to Body Cord Tester mode: short-circuit any bottom socket with socket B or C on the top face.

---

### Lamé Testing Mode

Connect the lamé to the crocodile clip of a foil body cord (bottom connection), then touch it with the 4 mm pin or the M5 cap nut. A diamond shape appears on the display.

| Display | Resistance | Status |
|---------|-----------|--------|
| Green diamond | < 5 Ω | FIE-compliant ✅ |
| Yellow diamond | 5–10 Ω | Close to limit ⚠️ |
| Orange diamond | 10–20 Ω | Not FIE-compliant ❌ |
| Red diamond | > 20 Ω | Lamé not working ❌ |

To return to Body Cord Tester mode: short-circuit any bottom socket with socket A or B on the top face.

---

### Guard Testing Mode

Connect a weapon to the bottom connection and touch the guard with the M5 cap nut. A cross shape appears on the display.

| Display | Resistance | Status |
|---------|-----------|--------|
| Green cross | < 5 Ω | FIE-compliant ✅ |
| Yellow cross | 5–8 Ω | Close to limit ⚠️ |
| Orange cross | 8–20 Ω | Not FIE-compliant ❌ |
| Red cross | > 20 Ω | Guard not working ❌ |

To return to Body Cord Tester mode: short-circuit any bottom socket with socket A or B on the top face.

---

### Cable Reel Testing Mode

Short-circuit sockets A and B on the top face. An "R" appears on the display. Connect the cable reel using two épée body cords.

| Display | Resistance | Status |
|---------|-----------|--------|
| Solid green lines | < 10 Ω | FIE-compliant ✅ |
| Solid yellow lines | 10–20 Ω | Close to limit ⚠️ |
| Solid orange lines | > 20 Ω | Not FIE-compliant ❌ |
| Broken red lines | > 50 Ω | Connection likely open ❌ |

To return to Body Cord Tester mode: short-circuit sockets A and B on the top face again.

---

## Mode Summary

| Mode | How to enter | Display |
|------|-------------|---------|
| Body Cord Tester | Power-up default | Blinking dot |
| Épée | Connect épée + press tip | "E" |
| Foil | Connect foil (auto-detected) | "F" |
| Lamé | Connect lamé via foil cord + touch | Diamond |
| Guard | Connect weapon + touch guard with cap nut | Cross |
| Cable Reel | Short A+B on top face | "R" |

---

## Technical Details

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 |
| Display | 5×5 LED matrix, 4 colours (green / yellow / orange / red) |
| Display (pro variant) | OLED — shows exact measured resistance values |
| Connections | 4 mm sockets (A, B, C) + 3 mm socket on top face |
| Power | 3.7 V Li-ion battery, USB-C charging |
| Language | C++ |
| Build system | PlatformIO |

---

## Related Repositories

This device is part of a larger open source fencing electronics platform:

| Repo | Description |
|------|-------------|
| [esp32scoringdeviceMqtt](https://github.com/pietwauters/esp32scoringdeviceMqtt) | ESP32 scoring device (MQTT/JSON) — active development |
| [CYDRemoteControl](https://github.com/pietwauters/CYDRemoteControl) | Hardware remote control based on the Cheap Yellow Display |
| [CyranoPisteMonitor](https://github.com/pietwauters/CyranoPisteMonitor) | Browser-based live piste monitoring — no app required |
| [WeaponWireTester](https://github.com/pietwauters/WeaponWireTester) | Legacy tester — kept for documentation and reference |
| [esp32-scoring-device](https://github.com/pietwauters/esp32-scoring-device) | Legacy scoring device — kept for wiki and photos |

---

## Contributing

Contributions are welcome. Please open an issue before submitting a pull request for significant changes.

If you are an armourer or fencing official who has used this device, feedback on usability and real-world test results is especially valuable.

*(A CONTRIBUTING.md with more detail will be added shortly.)*

---

## Licence

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0). You are free to use, modify, and distribute this software, provided that any derivative works are also distributed under the same licence.

**Contributor Licence Agreement (CLA):** Contributors are asked to sign a CLA before their contributions can be merged. This ensures the project can be maintained and relicensed if needed in the future. Details will be provided when you open a pull request.

---

## Author

**Piet Wauters**
Member, FIE SEMI Commission | Member, EFC SEMI Commission
EFC SEMI delegate, European Championships Genova 2024/2025
[github.com/pietwauters](https://github.com/pietwauters)
