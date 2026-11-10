# Heltec WiFi LoRa 32 V3 / V3.2 — pinout and gotchas

**Board:** HTIT-WB32LA V3.x · ESP32-S3 · 8 MB flash · Semtech SX1262 · 868 MHz-capable
variant required. Complete configuration:
[`examples/heltec-wifi-lora-32-v3.yaml`](../../examples/heltec-wifi-lora-32-v3.yaml).

The pin mapping is **identical across V3.0 / V3.1 / V3.2** — Heltec kept a single board
definition. V3.2 changes are PCB-level (TCXO circuit, USB-serial, minor power).

Verified against three independent sources that agree:
`espressif/arduino-esp32` → `variants/heltec_wifi_lora_32_V3/pins_arduino.h`,
`meshtastic/firmware` → `variants/heltec_v3/variant.h`,
and ESPHome's own `sx126x` documentation example.

---

## SX1262 radio

| Signal | GPIO |
|---|---|
| SPI SCK | **9** |
| SPI MOSI | **10** |
| SPI MISO | **11** |
| NSS (CS) | **8** |
| RST (NRST) | **12** |
| BUSY | **13** |
| DIO1 (IRQ) | **14** |

These seven lines are hard-wired on the PCB and are **not** broken out to the headers.

**DIO2** drives the RF switch and **DIO3** supplies the TCXO at **1.8 V**. Neither is connected
to the ESP32-S3 — both are internal to the radio module. In ESPHome: `rf_switch: true`,
`tcxo_voltage: 1_8V`, `tcxo_delay: 5ms`.

---

## Traps

**1. Heltec mislabels DIO1 as DIO0.** Their `pins_arduino.h` contains `#define DIO0 14`. The
SX1262 has no DIO0 — its IO pins start at DIO1 — and the line physically wired to GPIO14 is
DIO1, the interrupt line. Much of Heltec's own software repeats the error.

**2. Older Heltec documentation circulates a wrong SPI mapping** (SCK 12 / MISO 13 / MOSI 11 /
CS 10). It is wrong. Use the table above.

**3. Omitting the TCXO voltage is the single most common cause of "the radio initialises fine
but never receives."** If `tcxo_voltage` is unset, the oscillator is never powered and the
radio silently fails to lock.

**4. Explicit SPI pins.** Several `BresserWeatherSensorReceiver` users on this board saw
"init OK, no packets" until SPI was initialised with explicit pins before radio setup. ESPHome's
`spi:` component with explicit pins avoids this, but it is the first thing to check if the
radio goes quiet.

---

## Frequency variant

The V3 is sold in 433, 470–510, 863–870, 902–928 and 863–928 MHz variants. You need one that
covers **868.3 MHz** — the 863–870 or the wide-band 863–928 variant. The wide-band variant is
validated here; its matching network is flatter rather than optimal at any single frequency,
which is a non-issue for receive at 868.3.

**The PCB marking does not identify the band.** Boards are marked `HTIT-WB32LAF`, but Heltec's
own schematic is titled `HTIT-WB32LA(F)_V3.1` — the `(F)` is a revision marker printed on
boards of every band — and retailers list `HTIT-WB32LAF_V3.1-433MHZ` as a 433 MHz product. The
SX1262 die is band-agnostic; what differs is the RF matching network and filtering around it.
**The packaging, purchase record or seller listing is the only reliable source.**

A band-mismatched board still receives, just deaf — weak or absent frames at 868 MHz with a
known-good station nearby point at the front end. Receive-only use carries no damage risk.

**Antenna:** use the IPEX/U.FL port.

---

## Peripherals

Not used by the component; listed because they are commonly mis-documented.

| Function | GPIO | Notes |
|---|---|---|
| OLED SDA | 17 | Not on headers |
| OLED SCL | 18 | Not on headers |
| OLED RST | 21 | |
| White LED | 35 | Active HIGH, PWM-capable. A plain LED, not a WS2812. |
| Vext control | 36 | **Active LOW** (P-channel MOSFET) — use `inverted: true` |
| PRG / user button | 0 | Strapping pin, active LOW |
| Battery ADC (VBAT_READ) | 1 | ADC1_CH0 |
| ADC control (ADC_Ctrl) | 37 | **Must be driven LOW** to enable the divider |
| Header I²C (default) | SDA 41 / SCL 42 | Separate bus from the OLED |
| UART0 | TX 43 / RX 44 | |

### Battery measurement

The divider is VBAT — 390 kΩ — GPIO1 — 100 kΩ — GND, giving
`VBAT = V_ADC × (100 + 390) / 100`, i.e. **multiplier 4.9**.

Two things many published ESPHome configs for this board get wrong:

- **GPIO37 must be driven LOW** to enable the divider; leave it high otherwise so the divider
  does not drain the battery. Configs that omit GPIO37 read garbage.
- The divider impedance is high, so **low ADC attenuation** is appropriate (Meshtastic uses
  2.5 dB). The commonly copied `attenuation: 12db` with `multiply: 2.0` is wrong for this board.

Polarity caveat: ADC_Ctrl is documented active-LOW for V3, but Heltec used active-HIGH on some
sibling boards. If readings look inverted or pinned, try the other polarity.
