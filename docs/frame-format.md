# Bresser frame format

What the component decodes, byte by byte. The implementation is
[`components/bresser/bresser_decoder.cpp`](../components/bresser/bresser_decoder.cpp), a port
of rtl_433's [`bresser_7in1.c`](https://github.com/merbanan/rtl_433/blob/master/src/devices/bresser_7in1.c);
[`tests/run_golden.py`](../tests/run_golden.py) checks that both produce the same readings.

## On air

| Element | Value |
|---|---|
| Frequency | 868.3 MHz |
| Modulation | 2-FSK, NRZ |
| Bit period | 124 µs (~8.06 kbit/s; the radios run at 8210 baud) |
| Preamble | `AA AA AA AA AA` |
| Sync word | `2D D4` |
| Message | 25 bytes used, every byte whitened with XOR `0xAA`; stations pad to ~33 |
| Transmit interval | ~12 s (weather stations) |

The radio's packet engine strips preamble and sync word, so the component receives the
message starting with the first byte after `2D D4`. It needs at least 25 bytes; the examples
capture 32.

## Byte 6 is read before de-whitening

The decoder reads three fields from the raw on-air byte 6, and only then XORs the message
with `0xAA`:

| Bits of raw byte 6 | Field |
|---|---|
| 7–4 | sensor type |
| 3 | *not-startup* flag: 0 for the first frames after power-up |
| 2–0 | channel (air quality sensors) |

Reading it after de-whitening is a silent error: a 7-in-1 station's raw `0x18` (type 1)
becomes `0xB2` (type 11, the HCHO/VOC sensor), the digest still passes, and the frame decodes
as formaldehyde. This is also why **whitening must be off in the radio**.

A frame whose raw byte 21 is `0x00` is rejected before anything else.

## Integrity check

After de-whitening, bytes 0–1 hold a digest over bytes 2–24: an LFSR-16 hash with generator
`0x8810` and key `0xBA95`. The frame is valid when

```
((byte0 << 8) | byte1) ^ lfsr_digest16(bytes 2..24) == 0x6DF1
```

There is no CRC; the radio's own CRC must be off.

## Layout after de-whitening

All multi-digit values are BCD, one digit per nibble. "hi"/"lo" are the high and low nibble.

| Bytes | Weather (types 1, 12, 13) | Air PM (8) | CO₂ (10) | HCHO/VOC (11) |
|---|---|---|---|---|
| 0–1 | digest | digest | digest | digest |
| 2–3 | id (16-bit) | id | id | id |
| 4–5 | wind direction: 4.hi 4.lo 5.hi, ° | | CO₂, 4 digits, ppm | HCHO, 4 digits, ppb |
| 6 | type / startup / channel (raw) | ← | ← | ← |
| 7–8 | wind gust: 7.hi 7.lo 8.hi, ×0.1 m/s | | | |
| 8–9 | wind average: 8.lo 9.hi 9.lo, ×0.1 m/s | | | |
| 10–12 | rain, 6 digits, ×0.1 mm | PM2.5: 10.lo 11.hi 11.lo 12.hi, µg/m³ | | |
| 12–14 | | PM10: 12.lo 13.hi 13.lo 14.hi, µg/m³ | | |
| 14–15 | temperature: 14.hi 14.lo 15.hi, ×0.1 °C; above 600 subtract 1000 | | | |
| 15 (lo) | flags: battery low when bits 1–2 are both set | ← | ← | ← |
| 16 | humidity, 2 digits, % | | | |
| 17–19 | light, 6 digits, lux | | | |
| 20–21 | UV index: 20.hi 20.lo 21.hi, ×0.1 | | | |
| 22–23 | globe temperature (type 13): 22.hi 22.lo . 23.hi °C | | | 22.lo: VOC level 1–5 |

Details that matter:

- **3-in-1 (type 12)** has no wind, light or UV sensors; those fields are not reported.
- **8-in-1 (type 13)** reports a globe temperature only when the digit in 23.hi is a valid
  decimal digit (below 10).
- **Air quality sensors** send `0xF` in place of a value they have not measured yet (after
  power-up). A value is reported as absent when its marker nibble is `0xF`: 10.lo for PM2.5,
  12.lo for PM10, 5.lo for CO₂ and HCHO, 22.lo for VOC.
- The id changes when a sensor's batteries are replaced.

## Sensor types

| Type | Product | `model:` |
|---|---|---|
| 1 | 7-in-1 weather station | `bresser_7in1` |
| 13 | 8-in-1 weather station (7-in-1 plus globe thermometer) | `bresser_8in1` |
| 12 | 3-in-1 weather station (PN 7002530) | `bresser_3in1` |
| 8 | Air quality PM2.5/PM10 (PN 7009970) | `bresser_air_pm` |
| 10 | CO₂ (PN 7009977) | `bresser_co2` |
| 11 | HCHO/VOC (PN 7009978) | `bresser_hchovoc` |

Frames of any other type are ignored.
