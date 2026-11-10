# ESP32-S3 Super Mini + CC1101 — wiring and gotchas

An ESP32-S3 Super Mini with an 8-pin CC1101 breakout module (868 MHz version) on a breadboard.
Complete configuration: [`examples/esp32-s3-cc1101.yaml`](../../examples/esp32-s3-cc1101.yaml).
The radio profile and its measurements are in
[`docs/radio-settings.md`](../radio-settings.md#cc1101).

Nothing in [`heltec-wifi-lora-32-v3.md`](heltec-wifi-lora-32-v3.md) applies to this board.
Both are ESP32-S3, so a mixed-up pin map produces firmware that builds and flashes cleanly and
does nothing.

## Radio module

**There is more than one CC1101 breakout pin order in circulation.** Do not copy a pin table
from a listing, a screenshot or another module — verify it by the method in the next section.

### Validated wiring

| Module pin | Signal | ESP32-S3 Super Mini | ESPHome key |
|---|---|---|---|
| 1 | VCC | **3V3** | — |
| 2 | GND | GND | — |
| 3 | MOSI | GPIO5 | `spi: mosi_pin` |
| 4 | SCK | GPIO4 | `spi: clk_pin` |
| 5 | MISO (GDO1) | GPIO6 | `spi: miso_pin` |
| 6 | GDO2 | GPIO1 | *unused* |
| 7 | **GDO0** | **GPIO2** | `cc1101: gdo0_pin` |
| 8 | CSN | GPIO7 | `cc1101: cs_pin` |

### Another common layout (E07-M1101D style), for contrast

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|
| GND | VCC | GDO0 | CSN | SCK | MOSI | MISO | GDO2 |

Every pin differs: power is *reversed*, the SPI signals are permuted, and GDO0 moves from
pin 7 to pin 3.

### Verifying an unmarked module

1. **SPI proves six of the eight pins at once.** If `dump_config` reports a plausible chip ID
   (`0x0014` and `0x0004` are both seen in practice) then VCC, GND, SCK, MOSI, MISO and CSN are
   correct — a permuted SPI bus reads `0x0000` or `0xFFFF`, not a valid part number. Only the
   two GDO pins remain in doubt.
2. **The two GDO pins tell themselves apart by behaviour.** The driver sets GDO0 to FIFO status,
   which asserts only at a packet boundary, and leaves GDO2 at its default (serial data), which
   free-runs with the demodulator output. Point `gdo0_pin` at one candidate and watch the log:
   - **a flood of `RX FIFO overflow, flushing`** — tens of thousands per capture — means that
     pin is **GDO2**. The interrupt fires constantly, the driver finds no packet, and flushes.
     (The message is misleading: ESPHome prints it for an empty FIFO as well as a genuine
     overflow.)
   - **silence** means it is **GDO0**, or that the pin is not connected. Try the other
     candidate — one of them must free-run.
3. That flood is also the only positive proof available from software that **the receiver is
   demodulating**, since it is the demodulator's own output driving the pin.

**VCC is 3V3, not 5V.** The CC1101 is a 3.3 V part with no tolerance for 5 V on any pin.

**MISO and GDO1 share a pin.** That is normal for the CC1101 — GDO1 is the SPI data output
while CS is low. Do not use GDO1 as a status output while SPI is in use.

**GDO2 is wired but unused.** ESPHome's `cc1101` component drives only GDO0. Having it
connected costs nothing.

## Power: add a bulk capacitor

Fit a **47 µF capacitor across 3V3/GND** next to the module. Without it, the board browned out
during the reboot after an OTA update, inside ESP-IDF's safe-mode validity window, and the
bootloader rolled back to the *previous* firmware — deterministically — while the upload
reported success. A board in that state silently runs old firmware, and nothing on the log
says so.

## GDO0 is not optional

In packet mode `gdo0_pin` is **required**, and ESPHome's validator enforces it. GDO0 asserts at
end-of-packet and drives the interrupt that wakes `loop()` to drain the RX FIFO. Nothing is
polled: without that pin the radio receives correctly and no frame is ever read out.

## The chip does not start receiving by itself

There is no `rx_start: true` on this component. `setup()` configures the chip and attaches the
interrupt, then leaves it in IDLE. A receive-only configuration therefore needs:

```yaml
esphome:
  on_boot:
    - priority: -100
      then:
        - cc1101.begin_rx: radio
```

Priority `-100` runs it after every component's `setup()`, with SPI and the CC1101 both up
before the RX strobe. Omit it and the board reports a healthy chip ID and hears nothing,
forever — which reads exactly like a wiring fault.

## Board notes

- **`variant: esp32s3` with no `board:`.** ESPHome picks a default for the variant, which works
  on this board.
- **The on-board RGB LED's pin varies between Super Mini clones** (GPIO21 or GPIO48 are both
  common). It is cosmetic and blocks nothing.
- **No TCXO, no RF switch, no reset or busy pin.** Those are SX126x concerns; the CC1101 needs
  none of them, which is why its YAML block is so much shorter.
