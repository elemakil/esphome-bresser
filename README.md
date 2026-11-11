# esphome-bresser

An [ESPHome](https://esphome.io) external component that receives **Bresser 868 MHz weather
and air quality sensors** with an SX126x, SX127x or CC1101 radio and publishes their readings as
Home Assistant entities.

The radio's packet engine captures each frame; the component checks its digest, decodes it
and routes the reading to the entities you configured. Sensors you have not configured are
listed, so you can find their ids.

## Supported sensors

| Product | `model:` | Fields |
|---|---|---|
| 7-in-1 weather station | `bresser_7in1` | `temperature`, `humidity`, `rain`, `wind_speed`, `wind_gust`, `wind_direction`, `illuminance`, `uv_index` |
| 8-in-1 weather station | `bresser_8in1` | as 7-in-1, plus `globe_temperature` |
| 3-in-1 weather station (PN 7002530) | `bresser_3in1` | `temperature`, `humidity`, `rain` |
| Air quality PM2.5/PM10 (PN 7009970) | `bresser_air_pm` | `pm2_5`, `pm10`, `channel` |
| CO₂ (PN 7009977) | `bresser_co2` | `co2`, `channel` |
| HCHO/VOC (PN 7009978) | `bresser_hchovoc` | `hcho`, `voc_level`, `channel` |

Every model also has `rssi` (sensor), `battery_low`, `startup`, `online` (binary sensors),
`last_seen` and `transmitter_id` (text sensors).

The 7-in-1 has been tested with a real station. The other products use the same frame format
and are decoded exactly as rtl_433 decodes them (see [Tests](#development)), but have not been
tried on hardware here.

## Supported radios and boards

| Board | Radio | Example config | Notes |
|---|---|---|---|
| Heltec WiFi LoRa 32 V3 | SX1262 | [`examples/heltec-wifi-lora-32-v3.yaml`](examples/heltec-wifi-lora-32-v3.yaml) | [hardware notes](docs/hardware/heltec-wifi-lora-32-v3.md) |
| ESP32-S3 Super Mini + CC1101 breakout | CC1101 | [`examples/esp32-s3-cc1101.yaml`](examples/esp32-s3-cc1101.yaml) | [hardware notes](docs/hardware/esp32-s3-super-mini-cc1101.md) |
| LILYGO TTGO LoRa32-OLED V2.1.6 (T3 V1.6.1), 868 MHz | SX1276 | [`examples/ttgo-lora32-v2-1-6.yaml`](examples/ttgo-lora32-v2-1-6.yaml) | shows readings on the OLED; no signal strength (see [radio settings](docs/radio-settings.md)) |

Any board with an ESPHome `sx126x`, `sx127x` or `cc1101` radio should work with the same radio
settings; [docs/radio-settings.md](docs/radio-settings.md) explains them. All examples need
ESPHome 2026.10 or later.

## Quick start

1. Copy the example for your board and `secrets.yaml.example` (as `secrets.yaml`) into your
   ESPHome configuration directory, and fill in the secrets.
2. Flash it. The radio block in the example is already set up for Bresser frames.
3. Watch the log. Every Bresser frame produces a line, and the first frame since boot from a
   sensor you have not configured produces a hint, logged once per device:

   ```
   [I][bresser]: Unknown device: Bresser 7-in-1 id=12345 rssi=-71.0 -> add "model: bresser_7in1" and, if more than one is in range, "sensor_id: 12345"
   ```

4. Adjust `model:` (and, if needed, `sensor_id:`) in the entity blocks and flash again.

The essential part of a configuration:

```yaml
external_components:
  - source: github://elemakil/esphome-bresser@main
    components: [bresser]

bresser:
  radio_id: radio   # your sx126x: or cc1101: component

sensor:
  - platform: bresser
    model: bresser_7in1
    temperature:
      name: "Temperature"
    humidity:
      name: "Humidity"
```

## Configuration

### Hub: `bresser:`

```yaml
bresser:
  id: bresser_hub
  radio_id: radio
  on_reading:
    - lambda: 'ESP_LOGI("main", "%u %.1f", x.id, x.temperature.value_or(NAN));'
```

- **radio_id** (**Required**, ID): an `sx126x` or `cc1101` component. The component registers
  itself as a listener, so the radio's own `on_packet` trigger keeps working.
- **id** (*Optional*, ID): needed only if you reference the hub as `bresser_id`.
- **on_reading** (*Optional*, [Automation](https://esphome.io/automations/)): runs for every
  valid frame, whether or not an entity block matches it. The variable `x` is a
  `const bresser::Reading &`, see [Reading](#the-reading-passed-to-on_reading).

### Entity blocks: `sensor:`, `binary_sensor:`, `text_sensor:`

Each block describes one physical sensor. Blocks on different platforms with the same `model`
and `sensor_id` describe the same sensor, so `online` and `last_seen` reflect all of them.

```yaml
sensor:
  - platform: bresser
    model: bresser_7in1
    sensor_id: 12345
    device_id: dev_bresser
    temperature:
      name: "Temperature"
    rssi:
      name: "Signal"

binary_sensor:
  - platform: bresser
    model: bresser_7in1
    sensor_id: 12345
    device_id: dev_bresser
    battery_low:
      name: "Battery"
    online:
      name: "Online"
      timeout: 2min

text_sensor:
  - platform: bresser
    model: bresser_7in1
    sensor_id: 12345
    device_id: dev_bresser
    last_seen:
      name: "Last Seen"
```

- **model** (**Required**): one of the `model:` keys in the [table](#supported-sensors).
- **sensor_id** (*Optional*, 0–65535): only take frames from this id. Without it the block
  takes every sensor of that model. If any block of a model sets `sensor_id`, every block of
  that model must; otherwise the block without an id would also receive the other sensor's
  readings, and the configuration is rejected.
- **device_id** (*Optional*): a Home Assistant [sub-device](https://esphome.io/components/esphome/#esphome-devices)
  from `esphome: devices:`, applied to every entity in the block. An entity can set its own
  `device_id` to override it.
- **bresser_id** (*Optional*): the hub, if you gave it an `id`.

Then one entry per field, each taking the usual options of its platform (`name`, `filters`,
`icon`, ...). A field the chosen model does not have is rejected.

#### Sensor fields

| Field | Unit | Device class | Notes |
|---|---|---|---|
| `temperature` | °C | temperature | |
| `humidity` | % | humidity | |
| `rain` | mm | precipitation | Running total, `state_class: total_increasing`; restarts at 0 after a battery change. |
| `wind_speed` | m/s | wind_speed | Average. |
| `wind_gust` | m/s | wind_speed | |
| `wind_direction` | ° | wind_direction | `state_class: measurement_angle`. |
| `illuminance` | lx | illuminance | |
| `uv_index` | UV index | | |
| `globe_temperature` | °C | temperature | 8-in-1 only, when a globe thermometer is fitted. |
| `pm2_5` | µg/m³ | pm25 | |
| `pm10` | µg/m³ | pm10 | |
| `co2` | ppm | carbon_dioxide | |
| `hcho` | ppb | volatile_organic_compounds_parts | Formaldehyde. |
| `voc_level` | | | 1 (bad) to 5 (good). |
| `channel` | | | Channel switch of an air quality sensor. Diagnostic. |
| `rssi` | dBm | signal_strength | Of the last frame. Diagnostic. Unknown with an SX127x radio, which reports no RSSI in FSK mode. |

All have `state_class: measurement` unless noted (`rain`, `wind_direction`). A measurement is published only when the
frame carries it: air quality sensors send no value while warming up after power-on.

#### Binary sensor fields

| Field | Device class | Meaning |
|---|---|---|
| `battery_low` | battery | ON when the battery is low. |
| `startup` | | ON for the first frames after the sensor powered up. Diagnostic. |
| `online` | connectivity | ON while frames arrive. Diagnostic. |

`online` takes one extra option, **timeout** (*Optional*, default `2min`, at most 24 days): it turns OFF when
no frame from the sensor has arrived for this long. After boot it stays unknown until the
first frame, or turns OFF once `timeout` has passed with nothing heard. A weather station
sends every ~12 s, so the default tolerates several missed frames.

#### Text sensor fields

| Field | Device class | Meaning |
|---|---|---|
| `last_seen` | timestamp | Time of the last frame, ISO 8601 UTC. Diagnostic. |
| `transmitter_id` | | The id in the last frame, i.e. the value for `sensor_id`. Diagnostic. |

`last_seen` needs a [time](https://esphome.io/components/time/) component, e.g.
`time: - platform: homeassistant`. Until the clock is set, nothing is published.

#### Unknown devices

A `text_sensor` block of its own, without `model`, `sensor_id` or `device_id`:

```yaml
text_sensor:
  - platform: bresser
    unknown_devices:
      name: "Unknown Bresser devices"
```

It lists the Bresser sensors in range that no block matches, most recently heard first, as
`<model> <id> <rssi>dBm` separated by `; `, for example `7in1 12345 -71dBm; co2 4711 -80dBm`
(without the dBm part on an SX127x radio).
The model is the `model:` key without the `bresser_` prefix. When there are none it shows
`none`.

The list holds up to 8 sensors; the least recently heard one makes way for a new one, and a
sensor is dropped after 10 minutes without a frame. A change in which sensors are listed is
published at most every 5 s; changes in signal strength or order alone at most every 30 s. The
text is limited to 255 characters.

### The reading passed to `on_reading`

`x` is a `const esphome::bresser::Reading &`:

| Member | Type | |
|---|---|---|
| `id` | `uint16_t` | Sensor id. |
| `type` | `uint8_t` | 1 = 7-in-1, 13 = 8-in-1, 12 = 3-in-1, 8 = air PM, 10 = CO₂, 11 = HCHO/VOC. |
| `channel` | `uint8_t` | |
| `startup` | `bool` | |
| `battery_ok` | `bool` | |
| `rssi` | `float` | dBm; `NAN` with an SX127x radio. |
| `temperature`, `humidity`, `wind_gust`, `wind_speed`, `wind_direction`, `rain`, `illuminance_lux`, `uv_index`, `globe_temperature`, `pm2_5`, `pm10`, `co2`, `hcho`, `voc_level` | `std::optional<float>` | Units as in the sensor table. Empty when the frame does not carry the value. |

```yaml
bresser:
  radio_id: radio
  on_reading:
    - if:
        condition:
          lambda: 'return x.type == 1 && x.id == 12345 && x.wind_gust.value_or(0) > 15;'
        then:
          - logger.log: "Storm!"
```

## Finding your sensor's id

With a single sensor of each model in range you do not need `sensor_id`. If a neighbour's
sensor of the same model is in range too, find yours:

- With one sensor of a model in range, add `transmitter_id` to its block: the entity shows the
  id directly in Home Assistant.
- The log prints one line per frame at `DEBUG` level, for example
  `Bresser 7-in-1 id=12345 ch=0 rssi=-71.0 (not configured)`, and an `INFO` hint the first
  time since boot that an unconfigured sensor is heard.
- The `unknown_devices` entity lists every unconfigured sensor with its signal strength. The
  strongest is usually the closest.
- The id can change when you replace a sensor's batteries. Note the new one from the log or
  `unknown_devices` and update `sensor_id`.

## Troubleshooting

- **No Bresser lines in the log at all.** Check that the radio receives anything: the
  examples log every packet from the radio's `on_packet` trigger. No packets means a radio or
  wiring problem (see the hardware notes); for the CC1101, check the `on_boot` →
  `cc1101.begin_rx` action. Packets that never decode usually mean wrong radio settings; set
  the logger to `VERBOSE` to see rejected packets, and compare against
  [docs/radio-settings.md](docs/radio-settings.md). CRC and whitening must be off.
- **Frames decode but entities stay empty.** The log line ends in `(not configured)`: the
  `model` is wrong, or `sensor_id` does not match (it changes with new batteries).
- **`last_seen` never updates.** There is no `time:` component, or it has not synced yet.
- **CC1101 hears much less than expected.** Set `max_dvga_gain: "Default"`; see
  [docs/radio-settings.md](docs/radio-settings.md#max_dvga_gain-default).

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md). In short:

```sh
python3 tests/run_golden.py         # decoder vs. rtl_433 on 1556 payloads
tools/config_test/run.sh            # config validation, good and bad cases
tools/build_examples.sh compile     # build both examples against this checkout
```

Further reading: [docs/frame-format.md](docs/frame-format.md) (the frame, byte by byte) and
[docs/radio-settings.md](docs/radio-settings.md).

## Licence

GPL-3.0, see [LICENSE](LICENSE).

The decoder is a C++ port of [rtl_433](https://github.com/merbanan/rtl_433)'s
`bresser_7in1.c` by Christian W. Zuckschwerdt and the rtl_433 contributors (GPL-2.0-or-later).
Thanks to them for working out this protocol, and to
[BresserWeatherSensorReceiver](https://github.com/matthias-bs/BresserWeatherSensorReceiver)
for the radio settings.

## AI assistance

This component was developed with the help of AI coding assistants, under human direction
and review. The decoder is checked against rtl_433's output on 1556 test frames, and every
change goes through the same tests and CI builds as hand-written code.
