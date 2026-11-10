# Radio settings

The component does not configure the radio. It registers as a listener on an ESPHome
`sx126x`, `sx127x` or `cc1101` component and decodes whatever packets that radio delivers, so the
radio block in your YAML has to be set up for Bresser frames. The
[examples](../examples/) carry these settings with comments; this page explains them.

## Why the packet engine and a sync word

Both chips have a packet engine: it detects the preamble, matches a sync word, then captures a
fixed number of bytes into a FIFO and raises an interrupt. Using it means the ESP32 only sees
whole candidate frames, never a raw bit stream. Matching on Bresser's `2D D4` sync word makes
the delivered payload start exactly at the first message byte, which is what the decoder
expects (see [frame-format.md](frame-format.md)).

Two radio features must be **off**: CRC (the frame has its own digest, which the decoder
checks) and whitening (the frame is whitened with a plain XOR `0xAA`, which the decoder
undoes; radio-side de-whitening would destroy byte 6, which is read raw).

A sync match is not proof of a Bresser frame: noise and other 868 MHz traffic can match too.
Such packets fail the digest and are dropped (logged at `VERBOSE` level).

## SX126x (validated on Heltec WiFi LoRa 32 V3)

Based on [`matthias-bs/BresserWeatherSensorReceiver`](https://github.com/matthias-bs/BresserWeatherSensorReceiver),
which runs this configuration on the same board.

| Parameter | Value | Notes |
|---|---|---|
| `frequency` | 868.3 MHz | |
| `bitrate` | 8210 | 124 µs bit period. |
| `deviation` | 57.1 kHz | |
| `bandwidth` | `234_3kHz` | SX126x-specific enumerated value. |
| `shaping` | `NONE` | |
| `sync_value` | `[0xAA, 0xAA, 0x2D, 0xD4]` | Four bytes; see below. |
| `preamble_detect` | 1 | See below. |
| `payload_length` | 32 | At least 25 needed. |
| `crc_enable`, `whitening_enable` | `false` | |
| `rx_start` | `true` | |

Board-level settings (`tcxo_voltage: 1_8V`, `rf_switch: true`) are in
[hardware/heltec-wifi-lora-32-v3.md](hardware/heltec-wifi-lora-32-v3.md).

### Sync word: four bytes

The SX126x supports sync words up to 8 bytes. Including the last two preamble bytes,
`AA AA 2D D4`, makes the match far more selective than the two-byte `2D D4`, so band noise is
less likely to start a reception. The payload still starts right after `D4`. Alignment is unambiguous because `2D D4` is not periodic.

### Preamble detect: 1

`preamble_detect` is how many bytes of preamble the radio must see before it looks for the
sync word. 1 is a known-good value: requiring more can miss frames whose preamble arrives
before the AGC has settled, and 0 (detector off) lets noise start receptions that then abort.

868 MHz reception varies over hours and with the environment, so judge any change to these
settings against a second receiver on the same transmitter, not by before/after rates on one.

## CC1101

The same profile translated to the CC1101's keys, validated on an ESP32-S3 Super Mini with an
8-pin CC1101 breakout, receiving the same station.

| SX126x | CC1101 | Same? |
|---|---|---|
| `frequency: 868.3MHz` | `frequency: 868.3MHz` | yes |
| `modulation: FSK` | `modulation_type: "2-FSK"` | yes |
| `bitrate: 8210` | `symbol_rate: 8210` | yes |
| `deviation: 57.1kHz` | `fsk_deviation: 57.1kHz` | yes |
| `bandwidth: 234_3kHz` | `filter_bandwidth: 234.3kHz` | quantises to 232.14 kHz |
| `sync_value: [0xAA, 0xAA, 0x2D, 0xD4]` | `sync_mode: "16/16"`, `sync1: 0x2D`, `sync0: 0xD4` | 16 bits, not 32 |
| `payload_length: 32` | `packet_mode: true`, `packet_length: 32` | yes |
| `crc_enable: false`, `whitening_enable: false` | `crc_enable: false`, `whitening: false` | yes |
| `rx_start: true` | `on_boot` → `cc1101.begin_rx` | **different** |
| — | `max_dvga_gain: "Default"` | **set explicitly** |
| — | `gdo0_pin` | **required** |

What to watch for:

- **The CC1101 does not start receiving by itself.** Add an `on_boot` action with priority
  `-100` (after SPI and the chip are set up) that calls `cc1101.begin_rx`. Without it the chip
  reports a healthy id and hears nothing.
- **`gdo0_pin` is required.** In packet mode GDO0 signals end-of-packet and drives the
  interrupt that drains the FIFO.
- **The sync word is 16 bits.** The CC1101 cannot match four bytes, so it qualifies on
  `2D D4` alone and sees more false syncs than the SX126x. They fail the digest and are
  dropped. `"30/32"` mode would require the sync word twice on air, which the station does not
  send.
- **Bandwidth quantises**: `BW = 26 MHz / (8 × (4 + M) × 2^E)`, so 234.3 kHz becomes
  E=1, M=3 → 232.14 kHz. `dump_config` prints what the chip got.

### `max_dvga_gain: "Default"`

ESPHome's default for `max_dvga_gain` is `"-3"`, which blocks the three highest digital-gain
steps (about 18 dB of AGC range). `"Default"` means the chip's own default: the full range.

Measured as a ratio against an SX1262 on the same station, 20 cm apart, over 400 paired
5-minute windows:

| | CC1101 | SX1262 | Ratio |
|---|---|---|---|
| `max_dvga_gain: "-3"` | 42 % | 63 % | 0.67 |
| `max_dvga_gain: "Default"` | **77 %** | 79 % | **0.97** |

With the full gain range the CC1101 is on par with the SX1262. With its full range the AGC can
reach its magnitude target, so the slicer is less likely to emit near-random bits that match
`2D D4` by chance.

Wiring and board notes:
[hardware/esp32-s3-super-mini-cc1101.md](hardware/esp32-s3-super-mini-cc1101.md).

## SX127x

The same profile on the SX1276/SX1278 (ESPHome `sx127x`), as used in
[`examples/ttgo-lora32-v2-1-6.yaml`](../examples/ttgo-lora32-v2-1-6.yaml). It has not yet been
confirmed with a station; the values are carried over from the SX126x and CC1101 profiles.

| SX126x | SX127x | Notes |
|---|---|---|
| `modulation: FSK` | `modulation: FSK`, `packet_mode: true` | |
| `bitrate: 8210` | `bitrate: 8210` | |
| `deviation: 57.1kHz` | `deviation: 57.1kHz` | |
| `bandwidth: 234_3kHz` | `bandwidth: 250_0kHz` | the nearest SX127x FSK receive bandwidth |
| `sync_value: [0xAA, 0xAA, 0x2D, 0xD4]` | same | the SX127x matches up to 8 sync bytes |
| `preamble_detect: 1` | `preamble_detect: 1` | |
| `payload_length: 32` | `payload_length: 32` | |
| `crc_enable: false` | `crc_enable: false` | |
| -- | `bitsync: true` | required by ESPHome in FSK mode |
| -- | `rx_floor: -94` | RSSI threshold for starting a reception; lower it if a distant station is not heard |

ESPHome's `sx127x` passes no RSSI to listeners in FSK packet mode, so with this radio the
`rssi` sensor stays unknown and logs show `rssi=nan`.

There is no whitening key to turn off: ESPHome's `sx127x` always configures the packet engine
without DC-free encoding (whitening or Manchester).

