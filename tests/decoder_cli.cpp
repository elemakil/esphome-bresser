// Host-side driver for the decoder: one hex payload per line on stdin, one JSON object (or
// `null`) per line on stdout. Built and used by run_golden.py; also handy for checking a
// captured frame (see tests/README.md).

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "bresser_decoder.h"

using esphome::bresser::Reading;

static void field(std::string &out, const char *key, const std::optional<float> &value) {
  if (!value)
    return;
  char buf[64];
  snprintf(buf, sizeof(buf), ", \"%s\": %.9g", key, *value);
  out += buf;
}

static std::string to_json(const Reading &r) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"id\": %u, \"type\": %u, \"channel\": %u, \"startup\": %s, \"battery_ok\": %s", r.id,
           r.type, r.channel, r.startup ? "true" : "false", r.battery_ok ? "true" : "false");
  std::string out = buf;
  field(out, "temperature", r.temperature);
  field(out, "humidity", r.humidity);
  field(out, "wind_gust", r.wind_gust);
  field(out, "wind_speed", r.wind_speed);
  field(out, "wind_direction", r.wind_direction);
  field(out, "rain", r.rain);
  field(out, "illuminance_lux", r.illuminance_lux);
  field(out, "uv_index", r.uv_index);
  field(out, "globe_temperature", r.globe_temperature);
  field(out, "pm2_5", r.pm2_5);
  field(out, "pm10", r.pm10);
  field(out, "co2", r.co2);
  field(out, "hcho", r.hcho);
  field(out, "voc_level", r.voc_level);
  return out + "}";
}

static bool parse_hex(const std::string &line, std::vector<uint8_t> &bytes) {
  std::string hex;
  for (char c : line)
    if (!isspace(static_cast<unsigned char>(c)))
      hex += c;
  if (hex.size() % 2 != 0)
    return false;
  bytes.clear();
  for (size_t i = 0; i < hex.size(); i += 2) {
    if (!isxdigit(static_cast<unsigned char>(hex[i])) || !isxdigit(static_cast<unsigned char>(hex[i + 1])))
      return false;
    bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return true;
}

int main() {
  std::string line;
  std::vector<uint8_t> bytes;
  while (std::getline(std::cin, line)) {
    if (!parse_hex(line, bytes)) {
      std::cerr << "not a hex string: " << line << "\n";
      return 2;
    }
    auto reading = esphome::bresser::decode(bytes.data(), bytes.size());
    std::cout << (reading ? to_json(*reading) : "null") << "\n";
  }
  return 0;
}
