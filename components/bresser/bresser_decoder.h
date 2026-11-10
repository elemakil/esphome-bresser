#pragma once

// Bresser 868 MHz frame decoder. Plain C++17, no ESPHome dependencies, so it also builds
// on the host for tests/decoder_cli.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace esphome {
namespace bresser {

/// Sensor types, from the upper nibble of byte 6 of the frame.
enum SensorType : uint8_t {
  TYPE_WEATHER_7IN1 = 1,
  TYPE_AIR_PM = 8,
  TYPE_CO2 = 10,
  TYPE_HCHO_VOC = 11,
  TYPE_WEATHER_3IN1 = 12,
  TYPE_WEATHER_8IN1 = 13,
};

/// One decoded frame. A measurement is std::nullopt when the sensor type does not carry it,
/// or when the sensor flagged it as not yet available (air sensors warming up).
struct Reading {
  uint16_t id{0};
  uint8_t type{0};  ///< a SensorType
  uint8_t channel{0};
  bool startup{false};  ///< set for the first frames after the sensor powers up
  bool battery_ok{false};
  float rssi{0.0f};  ///< dBm; filled in by the receiver, not part of the frame

  std::optional<float> temperature;        ///< °C
  std::optional<float> humidity;           ///< %
  std::optional<float> wind_gust;          ///< m/s
  std::optional<float> wind_speed;         ///< m/s
  std::optional<float> wind_direction;     ///< °
  std::optional<float> rain;               ///< mm, running total
  std::optional<float> illuminance_lux;    ///< lx
  std::optional<float> uv_index;           ///<
  std::optional<float> globe_temperature;  ///< °C
  std::optional<float> pm2_5;              ///< µg/m³
  std::optional<float> pm10;               ///< µg/m³
  std::optional<float> co2;                ///< ppm
  std::optional<float> hcho;               ///< ppb
  std::optional<float> voc_level;          ///< 1 (bad) .. 5 (good)
};

/// Decode the bytes that follow the `2D D4` sync word. Returns std::nullopt for anything
/// that is not a valid Bresser frame of a supported type.
std::optional<Reading> decode(const uint8_t *data, size_t len);

}  // namespace bresser
}  // namespace esphome
