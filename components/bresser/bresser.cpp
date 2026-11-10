#include "bresser.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace esphome {
namespace bresser {

static const char *const TAG = "bresser";

struct ModelName {
  uint8_t type;
  const char *key;   ///< YAML `model:` without the "bresser_" prefix
  const char *name;  ///< for humans
};
static const ModelName MODEL_NAMES[] = {
    {TYPE_WEATHER_7IN1, "7in1", "Bresser 7-in-1"},
    {TYPE_WEATHER_8IN1, "8in1", "Bresser 8-in-1"},
    {TYPE_WEATHER_3IN1, "3in1", "Bresser 3-in-1"},
    {TYPE_AIR_PM, "air_pm", "Bresser Air PM"},
    {TYPE_CO2, "co2", "Bresser CO2"},
    {TYPE_HCHO_VOC, "hchovoc", "Bresser HCHO/VOC"},
};

static const ModelName &model_name(uint8_t type) {
  for (const auto &m : MODEL_NAMES) {
    if (m.type == type)
      return m;
  }
  return MODEL_NAMES[0];  // unreachable: the decoder only returns the types above
}

// --- BresserDevice -----------------------------------------------------------------------

void BresserDevice::publish(const Reading &r, uint32_t now) {
  this->last_heard_ms_ = now;
  for (auto &b : this->sensors_) {
    const auto &value = r.*b.field;
    if (value.has_value())
      b.sensor->publish_state(*value);
  }
  if (this->channel_sensor_ != nullptr)
    this->channel_sensor_->publish_state(r.channel);
  if (this->rssi_sensor_ != nullptr)
    this->rssi_sensor_->publish_state(r.rssi);
  if (this->transmitter_id_sensor_ != nullptr)
    this->transmitter_id_sensor_->publish_state(r.id);
  // device_class battery: ON means the battery is low.
  if (this->battery_sensor_ != nullptr)
    this->battery_sensor_->publish_state(!r.battery_ok);
  if (this->startup_sensor_ != nullptr)
    this->startup_sensor_->publish_state(r.startup);
  if (this->online_sensor_ != nullptr)
    this->online_sensor_->publish_state(true);
  if (this->last_seen_sensor_ != nullptr) {
    // Until a time source has synced, the clock counts up from 1970; publish nothing then.
    ESPTime t = ESPTime::from_epoch_utc(::time(nullptr));
    if (t.is_valid())
      this->last_seen_sensor_->publish_state(t.strftime("%Y-%m-%dT%H:%M:%SZ"));
  }
}

void BresserDevice::check_online(uint32_t now) {
  auto *s = this->online_sensor_;
  if (s == nullptr || now - this->last_heard_ms_ < this->online_timeout_ms_)
    return;
  if (!s->has_state() || s->state)
    s->publish_state(false);
}

// --- BresserComponent --------------------------------------------------------------------

ReadingTrigger::ReadingTrigger(BresserComponent *parent) { parent->add_reading_trigger(this); }

BresserDevice *BresserComponent::get_device(uint8_t type, int32_t sensor_id) {
  for (auto *d : this->devices_) {
    if (d->type() == type && d->sensor_id() == sensor_id)
      return d;
  }
  auto *d = new BresserDevice(type, sensor_id);  // NOLINT(cppcoreguidelines-owning-memory)
  this->devices_.push_back(d);
  return d;
}

void BresserComponent::setup() {
#ifdef BRESSER_RADIO_SX126X
  if (this->sx126x_ != nullptr)
    this->sx126x_->register_listener(this);
#endif
#ifdef BRESSER_RADIO_CC1101
  if (this->cc1101_ != nullptr)
    this->cc1101_->register_listener(this);
#endif
#ifdef BRESSER_RADIO_SX127X
  if (this->sx127x_ != nullptr)
    this->sx127x_->register_listener(this);
#endif
  if (this->unknown_sensor_ != nullptr)
    this->publish_unknown_(millis());
  this->set_interval(1000, [this]() {
    const uint32_t now = millis();
    for (auto *d : this->devices_)
      d->check_online(now);
    this->expire_unknown_(now);
  });
}

void BresserComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Bresser:");
#ifdef BRESSER_RADIO_SX126X
  ESP_LOGCONFIG(TAG, "  Radio: SX126x");
#endif
#ifdef BRESSER_RADIO_CC1101
  ESP_LOGCONFIG(TAG, "  Radio: CC1101");
#endif
#ifdef BRESSER_RADIO_SX127X
  ESP_LOGCONFIG(TAG, "  Radio: SX127x");
#endif
  for (auto *d : this->devices_) {
    if (d->sensor_id() < 0) {
      ESP_LOGCONFIG(TAG, "  Device: %s, any id", model_name(d->type()).name);
    } else {
      ESP_LOGCONFIG(TAG, "  Device: %s, id %" PRId32, model_name(d->type()).name, d->sensor_id());
    }
  }
}

#if defined(BRESSER_RADIO_SX126X) || defined(BRESSER_RADIO_SX127X)
void BresserComponent::on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) {
  this->handle_packet_(packet, rssi);
}
#endif
#ifdef BRESSER_RADIO_CC1101
void BresserComponent::on_packet(const std::vector<uint8_t> &packet, float freq_offset, float rssi, uint8_t lqi) {
  this->handle_packet_(packet, rssi);
}
#endif

void BresserComponent::handle_packet_(const std::vector<uint8_t> &packet, float rssi) {
  auto reading = decode(packet.data(), packet.size());
  if (!reading.has_value()) {
    // Normal for noise that happened to match the sync word.
    ESP_LOGV(TAG, "Ignored %u-byte packet (rssi %.1f)", (unsigned) packet.size(), rssi);
    return;
  }
  Reading &r = *reading;
  r.rssi = rssi;
  const uint32_t now = millis();

  BresserDevice *device = nullptr;
  for (auto *d : this->devices_) {
    if (d->matches(r)) {
      device = d;
      break;
    }
  }
  ESP_LOGD(TAG, "%s id=%u ch=%u rssi=%.1f%s", model_name(r.type).name, r.id, r.channel, r.rssi,
           device != nullptr ? "" : " (not configured)");

  if (device != nullptr) {
    device->publish(r, now);
  } else {
    this->note_unknown_(r, now);
  }
  for (auto *t : this->triggers_)
    t->trigger(r);
}

void BresserComponent::note_unknown_(const Reading &r, uint32_t now) {
  auto it = std::find_if(this->unknown_.begin(), this->unknown_.end(),
                         [&r](const UnknownDevice &u) { return u.id == r.id && u.type == r.type; });
  if (it != this->unknown_.end()) {
    this->unknown_.erase(it);
  } else {
    // Hint once per device since boot, even if it has expired from the table and returns.
    if (this->take_hint_((uint32_t) r.type << 16 | r.id)) {
      const auto &m = model_name(r.type);
      ESP_LOGI(TAG,
               "Unknown device: %s id=%u rssi=%.1f -> add \"model: bresser_%s\" and, if more than one is in range, "
               "\"sensor_id: %u\"",
               m.name, r.id, r.rssi, m.key, r.id);
    }
    if (this->unknown_.size() >= MAX_UNKNOWN)
      this->unknown_.pop_back();  // the least recently heard
  }
  this->unknown_.insert(this->unknown_.begin(), UnknownDevice{r.id, r.type, r.rssi, now});
  this->unknown_dirty_ = true;
  this->publish_unknown_(now);
}

bool BresserComponent::take_hint_(uint32_t key) {
  for (size_t i = 0; i < this->hinted_count_; i++) {
    if (this->hinted_[i] == key)
      return false;
  }
  this->hinted_[this->hinted_next_] = key;
  this->hinted_next_ = (this->hinted_next_ + 1) % HINTED_SLOTS;
  if (this->hinted_count_ < HINTED_SLOTS)
    this->hinted_count_++;
  return true;
}

void BresserComponent::expire_unknown_(uint32_t now) {
  auto last = std::remove_if(this->unknown_.begin(), this->unknown_.end(),
                             [now](const UnknownDevice &u) { return now - u.last_ms >= UNKNOWN_EXPIRY_MS; });
  if (last != this->unknown_.end()) {
    this->unknown_.erase(last, this->unknown_.end());
    this->unknown_dirty_ = true;
  }
  // Runs every second, so this also flushes an update held back by the rate limit.
  this->publish_unknown_(now);
}

void BresserComponent::publish_unknown_(uint32_t now) {
  if (this->unknown_sensor_ == nullptr || !this->unknown_dirty_)
    return;
  // `text` is the published list, newest first; `set` identifies which devices are in it,
  // so that membership changes are rate-limited to one every 5 s and RSSI or order
  // changes to one every 30 s. A held-back update stays dirty and is flushed by the 1 s interval.
  std::string text;
  std::vector<uint32_t> keys;
  for (const auto &u : this->unknown_) {
    char entry[40];
    snprintf(entry, sizeof(entry), "%s%s %u %ddBm", text.empty() ? "" : "; ", model_name(u.type).key, u.id,
             (int) lroundf(u.rssi));
    if (text.size() + strlen(entry) > 255)
      break;
    text += entry;
    keys.push_back((uint32_t) u.type << 16 | u.id);
  }
  if (text.empty())
    text = "none";
  std::sort(keys.begin(), keys.end());
  std::string set;
  for (uint32_t k : keys)
    set += std::to_string(k) + ",";

  if (this->unknown_set_published_once_ && text == this->unknown_published_) {
    this->unknown_dirty_ = false;
    return;
  }
  const bool first = !this->unknown_set_published_once_;
  const bool set_changed = first || set != this->unknown_published_set_;
  if (set_changed) {
    if (!first && now - this->unknown_set_published_ms_ < UNKNOWN_SET_REPUBLISH_MS)
      return;
  } else if (now - this->unknown_published_ms_ < UNKNOWN_REPUBLISH_MS) {
    return;
  }
  this->unknown_published_ = text;
  this->unknown_published_set_ = set;
  this->unknown_published_ms_ = now;
  if (set_changed)
    this->unknown_set_published_ms_ = now;
  this->unknown_set_published_once_ = true;
  this->unknown_dirty_ = false;
  this->unknown_sensor_->publish_state(text);
}

}  // namespace bresser
}  // namespace esphome
