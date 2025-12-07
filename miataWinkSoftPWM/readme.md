# This version has SOFT-START PWM

✅ What you’ll notice immediately

    Motors start smooth
    Headlights glide instead of slam
    Softer on physical bits (lights, gears, motors)
    No relay noise
    Reduced electrical stress
    Can also change the speed later on.

## Key design choices

    - Relays are replaced by MOSFET low-side switching
    - Each motor direction gets:
        - its own MOSFET
        - its own PWM channel
    - PWM ramps from 0 → 100% over ~150 ms
    - Still non-blocking
    - Still state-machine-based




## Parts
You need 4 identical channels total (left up, left down, right up, right down):

✅ MOSFET (Must fully turn on at 3.3 V gate)


    Pick MOSFETs that:
        Are N-channel, logic-level (fully enhanced at 3.3 V gate since ESP32 drives 3.3V),
        Vds rating ≥ 30 – 60V (12V system, give margin),
        Low Rds_on (< 50 mΩ ideally),
        Package depending on current (TO-220 for easy heatsinking, or SMD if you have board).

    Use logic-level N-MOSFET, examples:
        IRLZ44N 

✅ Flyback diode (MANDATORY)

    Across each motor winding:
        STPS30L30 (30A)

✅ Gate resistor

    150 Ω – 330 Ω, ¼ W between ESP32 pin and MOSFET gate

✅ Pull-down resistor

    100 kΩ from gate → gnd (keeps motor off at boot) (motor -- gate -- 100k -- gnd)


✅ TVS

    1.5KE20CA [aliexpress](https://www.aliexpress.us/item/3256809377461245.html?spm=a2g0o.order_list.order_list_main.5.42c21802ET1c90&gatewayAdapt=glo2usa)

✅ Voltage supply

    5V buck converter [amazon](https://www.amazon.com/dp/B0DKTMGBHL?ref=ppx_yo2ov_dt_b_fed_asin_title)


✅ Wiring (low-side switching)
```
    ESP32 GPIO
      |
    Gate resistor
      |
    MOSFET GATE
      |
    100kΩ Pull down resistor
      |
    GND

    MOSFET SOURCE → GND
    MOSFET DRAIN  → motor negative
    motor positive → +12 V

    Flyback diode across motor

    TVS accross buck converter inputs
    buck converter output powering esp32
```
Repeat four times.


















