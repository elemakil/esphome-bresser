#pragma once

#include <array>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

// Only the configured radio's component is in the build, so its header is included behind a
// define that __init__.py emits for it.
#ifdef BRESSER_RADIO_SX126X
#include "esphome/components/sx126x/sx126x.h"
#endif
#ifdef BRESSER_RADIO_CC1101
#include "esphome/components/cc1101/cc1101.h"
#endif
#ifdef BRESSER_RADIO_SX127X
#include "esphome/components/sx127x/sx127x.h"
#endif

#include "bresser_decoder.h"

namespace esphome {
namespace bresser {

/// One configured sensor (model + optional id) and the entities that report it. All
/// sensor/binary_sensor/text_sensor blocks with the same model and sensor_id share one.
class BresserDevice {
 public:
  BresserDevice(uint8_t type, int32_t sensor_id) : type_(type), sensor_id_(sensor_id) {}

  bool matches(const Reading &r) const {
    return r.type == this->type_ && (this->sensor_id_ < 0 || this->sensor_id_ == r.id);
  }
  uint8_t type() const { return this->type_; }
  int32_t sensor_id() const { return this->sensor_id_; }

  void add_sensor(std::optional<float> Reading::*field, sensor::Sensor *s) { this->sensors_.push_back({field, s}); }
  void set_channel_sensor(sensor::Sensor *s) { this->channel_sensor_ = s; }
  void set_rssi_sensor(sensor::Sensor *s) { this->rssi_sensor_ = s; }
  void set_transmitter_id_sensor(sensor::Sensor *s) { this->transmitter_id_sensor_ = s; }
  void set_battery_sensor(binary_sensor::BinarySensor *s) { this->battery_sensor_ = s; }
  void set_startup_sensor(binary_sensor::BinarySensor *s) { this->startup_sensor_ = s; }
  void set_online_sensor(binary_sensor::BinarySensor *s, uint32_t timeout_ms) {
    this->online_sensor_ = s;
    this->online_timeout_ms_ = timeout_ms;
  }
  void set_last_seen_sensor(text_sensor::TextSensor *s) { this->last_seen_sensor_ = s; }

  void publish(const Reading &r, uint32_t now);
  /// Turns `online` off once no frame has arrived for the timeout.
  void check_online(uint32_t now);

 protected:
  struct SensorBinding {
    std::optional<float> Reading::*field;
    sensor::Sensor *sensor;
  };

  uint8_t type_;
  int32_t sensor_id_;  ///< -1: any id
  std::vector<SensorBinding> sensors_;
  sensor::Sensor *channel_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  sensor::Sensor *transmitter_id_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_sensor_{nullptr};
  binary_sensor::BinarySensor *startup_sensor_{nullptr};
  binary_sensor::BinarySensor *online_sensor_{nullptr};
  text_sensor::TextSensor *last_seen_sensor_{nullptr};
  uint32_t online_timeout_ms_{0};
  uint32_t last_heard_ms_{0};  ///< boot counts as "last heard" until the first frame
};

class BresserComponent;

/// Fires for every valid frame, whether or not a configured device matched it.
class ReadingTrigger : public Trigger<const Reading &> {
 public:
  explicit ReadingTrigger(BresserComponent *parent);
};

/// Receives packets from the radio, decodes them and dispatches the readings.
///
/// Each supported radio has its own listener interface; the hub implements the one for the
/// configured radio and funnels it into handle_packet_().
class BresserComponent : public Component
#ifdef BRESSER_RADIO_SX126X
    ,
                         public sx126x::SX126xListener
#endif
#ifdef BRESSER_RADIO_CC1101
    ,
                         public cc1101::CC1101Listener
#endif
#ifdef BRESSER_RADIO_SX127X
    ,
                         public sx127x::SX127xListener
#endif
{
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

#ifdef BRESSER_RADIO_SX126X
  void set_radio(sx126x::SX126x *radio) { this->sx126x_ = radio; }
#endif
#ifdef BRESSER_RADIO_SX127X
  void set_radio(sx127x::SX127x *radio) { this->sx127x_ = radio; }
#endif
#if defined(BRESSER_RADIO_SX126X) || defined(BRESSER_RADIO_SX127X)
  // SX126x and SX127x listeners share this signature.
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;
#endif
#ifdef BRESSER_RADIO_CC1101
  void set_radio(cc1101::CC1101Component *radio) { this->cc1101_ = radio; }
  void on_packet(const std::vector<uint8_t> &packet, float freq_offset, float rssi, uint8_t lqi) override;
#endif

  /// The device for (type, sensor_id), created on first use. sensor_id -1 means any id.
  BresserDevice *get_device(uint8_t type, int32_t sensor_id);
  void set_unknown_devices_sensor(text_sensor::TextSensor *s) { this->unknown_sensor_ = s; }
  void add_reading_trigger(ReadingTrigger *t) { this->triggers_.push_back(t); }

 protected:
  /// A valid frame no configured device claimed.
  struct UnknownDevice {
    uint16_t id;
    uint8_t type;
    float rssi;
    uint32_t last_ms;
  };
  static constexpr size_t MAX_UNKNOWN = 8;
  static constexpr uint32_t UNKNOWN_EXPIRY_MS = 10 * 60 * 1000;
  static constexpr uint32_t UNKNOWN_REPUBLISH_MS = 30 * 1000;     ///< RSSI- or order-only changes
  static constexpr uint32_t UNKNOWN_SET_REPUBLISH_MS = 5 * 1000;  ///< membership changes
  static constexpr size_t HINTED_SLOTS = 16;

  void handle_packet_(const std::vector<uint8_t> &packet, float rssi);
  void note_unknown_(const Reading &r, uint32_t now);
  void expire_unknown_(uint32_t now);
  void publish_unknown_(uint32_t now);
  bool take_hint_(uint32_t key);

#ifdef BRESSER_RADIO_SX126X
  sx126x::SX126x *sx126x_{nullptr};
#endif
#ifdef BRESSER_RADIO_CC1101
  cc1101::CC1101Component *cc1101_{nullptr};
#endif
#ifdef BRESSER_RADIO_SX127X
  sx127x::SX127x *sx127x_{nullptr};
#endif
  std::vector<BresserDevice *> devices_;
  std::vector<ReadingTrigger *> triggers_;
  std::vector<UnknownDevice> unknown_;  ///< most recently heard first
  text_sensor::TextSensor *unknown_sensor_{nullptr};
  std::string unknown_published_;
  std::string unknown_published_set_;
  uint32_t unknown_published_ms_{0};
  uint32_t unknown_set_published_ms_{0};
  bool unknown_dirty_{true};                ///< the table changed, or a rate-limited publish is pending
  bool unknown_set_published_once_{false};  ///< something has been published
  /// (type << 16 | id) of devices already hinted about, used as a ring and never expired.
  std::array<uint32_t, HINTED_SLOTS> hinted_{};
  size_t hinted_count_{0};
  size_t hinted_next_{0};
};

}  // namespace bresser
}  // namespace esphome
