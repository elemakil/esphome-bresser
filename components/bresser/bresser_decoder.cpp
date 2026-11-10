// Decoder for Bresser Weather Center 7-in-1 and Air quality sensors.
//
// A C++ port of rtl_433's src/devices/bresser_7in1.c:
//
//     Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
//
//     This program is free software; you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation; either version 2 of the License, or
//     (at your option) any later version.
//
// The port keeps the original's arithmetic and its quirks, so that it produces exactly the
// same readings; tests/run_golden.py checks that against rtl_433's own output.
//
// Frame layout, bytes after the 2D D4 sync word (details in docs/frame-format.md):
//
//   0-1    LFSR-16 digest over bytes 2-24
//   2-3    sensor id
//   4-5    wind direction (weather), CO2 ppm or HCHO ppb, BCD
//   6      type:4 nstartup:1 channel:3 -- read before de-whitening
//   7-9    wind gust and average, BCD, x0.1 m/s
//   10-12  rain, BCD, x0.1 mm; air PM: PM2.5 in 10-12, PM10 in 12-14
//   14-15  temperature, BCD, x0.1 °C; 15 low nibble: flags (battery low = bits 1-2 set)
//   16     humidity, BCD
//   17-19  light, BCD, lux
//   20-21  UV index, BCD, x0.1
//   22-23  globe temperature (8-in-1); 22 low nibble: VOC level (HCHO/VOC sensor)
//
// Every byte is whitened with XOR 0xAA. Byte 6 is read before de-whitening.

#include "bresser_decoder.h"

namespace esphome {
namespace bresser {

static constexpr size_t MSG_LEN = 25;

/// rtl_433's lfsr_digest16(): a keyed 16-bit LFSR hash over `len` bytes.
static uint16_t lfsr_digest16(const uint8_t *msg, size_t len, uint16_t gen, uint16_t key) {
  uint16_t sum = 0;
  for (size_t k = 0; k < len; k++) {
    for (int i = 7; i >= 0; i--) {
      if ((msg[k] >> i) & 1)
        sum ^= key;
      key = (key & 1) ? (key >> 1) ^ gen : key >> 1;
    }
  }
  return sum;
}

static inline int hi(uint8_t b) { return b >> 4; }
static inline int lo(uint8_t b) { return b & 0x0f; }

std::optional<Reading> decode(const uint8_t *data, size_t len) {
  if (data == nullptr || len < MSG_LEN)
    return std::nullopt;

  uint8_t msg[MSG_LEN];
  for (size_t i = 0; i < MSG_LEN; i++)
    msg[i] = data[i];

  // Sanity check and type/startup/channel all come from the raw, still-whitened bytes.
  if (msg[21] == 0x00)
    return std::nullopt;
  const int s_type = msg[6] >> 4;
  const bool nstartup = (msg[6] & 0x08) != 0;
  const int channel = msg[6] & 0x07;

  for (auto &b : msg)
    b ^= 0xaa;

  const int chk = (msg[0] << 8) | msg[1];
  const int digest = lfsr_digest16(&msg[2], 23, 0x8810, 0xba95);
  if ((chk ^ digest) != 0x6df1)
    return std::nullopt;

  Reading r;
  r.id = (msg[2] << 8) | msg[3];
  r.type = s_type;
  r.channel = channel;
  r.startup = !nstartup;
  r.battery_ok = (msg[15] & 0x06) != 0x06;

  switch (s_type) {
    case TYPE_WEATHER_7IN1:
    case TYPE_WEATHER_3IN1:
    case TYPE_WEATHER_8IN1: {
      const int wdir = hi(msg[4]) * 100 + lo(msg[4]) * 10 + hi(msg[5]);
      const int wgst_raw = hi(msg[7]) * 100 + lo(msg[7]) * 10 + hi(msg[8]);
      const int wavg_raw = lo(msg[8]) * 100 + hi(msg[9]) * 10 + lo(msg[9]);
      const int rain_raw = hi(msg[10]) * 100000 + lo(msg[10]) * 10000 + hi(msg[11]) * 1000 + lo(msg[11]) * 100 +
                           hi(msg[12]) * 10 + lo(msg[12]);
      const int temp_raw = hi(msg[14]) * 100 + lo(msg[14]) * 10 + hi(msg[15]);
      const int humidity = hi(msg[16]) * 10 + lo(msg[16]);
      const int lght_raw = hi(msg[17]) * 100000 + lo(msg[17]) * 10000 + hi(msg[18]) * 1000 + lo(msg[18]) * 100 +
                           hi(msg[19]) * 10 + lo(msg[19]);
      const int uv_raw = hi(msg[20]) * 100 + lo(msg[20]) * 10 + hi(msg[21]);

      r.temperature = temp_raw > 600 ? (temp_raw - 1000) * 0.1f : temp_raw * 0.1f;
      r.humidity = humidity;
      r.rain = rain_raw * 0.1f;
      // The 3-in-1 has no wind, light or UV sensors.
      if (s_type != TYPE_WEATHER_3IN1) {
        r.wind_gust = wgst_raw * 0.1f;
        r.wind_speed = wavg_raw * 0.1f;
        r.wind_direction = wdir;
        r.illuminance_lux = lght_raw;
        r.uv_index = uv_raw * 0.1f;
      }
      // Globe thermometer, 8-in-1 only; an invalid tenths digit means none is fitted.
      if (s_type == TYPE_WEATHER_8IN1 && hi(msg[23]) < 10)
        r.globe_temperature = hi(msg[22]) * 10 + lo(msg[22]) + hi(msg[23]) * 0.1f;
      return r;
    }

    case TYPE_AIR_PM:
      // A 0xF digit marks a value the sensor has not measured yet.
      if (lo(msg[10]) != 0x0f)
        r.pm2_5 = lo(msg[10]) * 1000 + hi(msg[11]) * 100 + lo(msg[11]) * 10 + hi(msg[12]);
      if (lo(msg[12]) != 0x0f)
        r.pm10 = lo(msg[12]) * 1000 + hi(msg[13]) * 100 + lo(msg[13]) * 10 + hi(msg[14]);
      return r;

    case TYPE_CO2:
      if (lo(msg[5]) != 0x0f)
        r.co2 = hi(msg[4]) * 1000 + lo(msg[4]) * 100 + hi(msg[5]) * 10 + lo(msg[5]);
      return r;

    case TYPE_HCHO_VOC:
      if (lo(msg[5]) != 0x0f)
        r.hcho = hi(msg[4]) * 1000 + lo(msg[4]) * 100 + hi(msg[5]) * 10 + lo(msg[5]);
      if (lo(msg[22]) != 0x0f)
        r.voc_level = lo(msg[22]);
      return r;

    default:
      return std::nullopt;
  }
}

}  // namespace bresser
}  // namespace esphome
