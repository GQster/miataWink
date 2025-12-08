# Miata Headlight Motor Controller (ESP32 + PWM Soft-Start)

This project replaces the factory relay-based Miata pop-up headlight control with an ESP32-based controller using **MOSFET low-side switching**, **PWM soft-start**, and a **non-blocking state machine**.

The intent is to make the headlights move smoothly, reduce mechanical and electrical shock, and enable more expressive motion (wink, split, wave) without abusing the motors or linkages.

This is **not** a “slam 12 V and hope the limit switch saves it” design.

---

## ✅ What you’ll notice immediately

- Motors start smoothly  
- Headlights glide instead of slam  
- Less stress on:
  - gears  
  - bushings  
  - linkages  
  - motor windings  
- No relay clicking  
- Reduced inrush current and EMI  
- Motion speed and behavior are fully tunable in software  

---

## 🎛 Supported behaviors

Triggered using a **single button** with multi-click detection:

| Action | Input |
|------|------|
| Toggle both headlights | Single click |
| Wink (alternating left / right) | Double click |
| Continuous wave (until pressed again) | Triple click |
| Split headlights | Quadruple click |
| Reset (force both down) | Long hold |

---

## 🧠 Key design choices

- Relays replaced with **logic-level N-MOSFETs**
- **One MOSFET + one PWM channel per motor direction**
  - Left up  
  - Left down  
  - Right up  
  - Right down  
- ESP32 hardware PWM:
  - ~1.5 kHz (quiet, motor-safe)
  - 8-bit resolution
- **Soft-start PWM ramp**
  - 0 → 100 % over ~150 ms
- Fully **non-blocking**
  - No `delay()`
  - Per-channel timers
- Explicit **state machine**
  - `IDLE`
  - `ACTIVE`
  - `WINKING`
  - `WAVING`
  - queued transitions handled cleanly
- Physical headlight position tracked **per side**
  - prevents long-term desync

---

## 🔧 Electrical overview

This is a **12 V automotive system using low-side switching**.

Each motor direction is independently driven, allowing overlapping movements (required for wave effects) without interrupting other motors.

### Channel count

You need **4 identical power channels total**:

- Left up  
- Left down  
- Right up  
- Right down  

---

## 🧩 Parts list

### ✅ MOSFET (required)

Must fully turn on at **3.3 V gate drive** (ESP32 GPIO level).

**Requirements**
- N-channel, logic-level
- Vds ≥ 30–60 V (12 V system + margin)
- Low Rds(on) (≤ 50 mΩ recommended)
- Package appropriate for current (TO-220 preferred)

**Known-good examples**
- `IRLZ44N`

> Automotive-rated MOSFETs are strongly recommended for in-car use.

---

### ✅ Flyback diode (mandatory)

One per motor.

- Fast, high-current diode
- Schottky preferred

**Example**
- `STPS30L30` (30 A)

Mount directly across the motor terminals.

---

### ✅ Gate resistor

- 150 Ω – 330 Ω
- Between ESP32 GPIO and MOSFET gate
- Limits gate charge current and ringing

---

### ✅ Gate pull-down resistor

- 100 kΩ from **gate → ground**
- Keeps MOSFET off during boot/reset

---

### ✅ Power supply

- 12 V → 5 V buck converter
- ESP32 powered from regulated 5 V (should be able to take in +30V)

Example:
- LM2596-style buck module

---

### ✅ TVS diode (strongly recommended)

Protects against load dumps and inductive spikes.
Pair this with your buck converter to make sure it shunts ***below*** its max Vin

- Example: `1.5KE20CA`
- Placed across the buck converter **input (12 V side)**

---

## 🔌 Wiring (low-side switching)

    ESP32 GPIO
     |
    Gate resistor (150–330Ω)
     |
    MOSFET GATE
     |
    100kΩ pull-down
     |
    GND

```MOSFET SOURCE → GND```

```MOSFET DRAIN → Motor negative```

```Motor positive → +12 V```

```Flyback diode across motor terminals (reverse-biased)```

```TVS diode across buck converter input```

```Buck output → ESP32 supply```





Repeat **four times**, once per motor direction.

---

## ⚠️ Automotive notes

- ESP32 and motor power **must share a common ground**
- Keep motor current paths short and thick
- Use a star-ground topology where possible
- Add bulk capacitance near motors if wiring is long
- This design assumes motors include **internal limit switches**
  - PWM timing is **not** used as a hard stop

---

## 🧪 Project status

- ✅ Tested with real headlight motors
- ✅ Continuous wave with overlapping motion
- ✅ Alternating wink works correctly
- ✅ No blocking, no hangs
- ✅ Clean recovery on reset

---

## 📜 License

MIT License

Use at your own risk.

