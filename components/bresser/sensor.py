import esphome.codegen as cg
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_CARBON_DIOXIDE,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_ILLUMINANCE,
    DEVICE_CLASS_PM10,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_PRECIPITATION,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
    DEVICE_CLASS_WIND_DIRECTION,
    DEVICE_CLASS_WIND_SPEED,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_MEASUREMENT_ANGLE,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_DEGREES,
    UNIT_LUX,
    UNIT_METER_PER_SECOND,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
    UNIT_MILLIMETER,
    UNIT_PARTS_PER_BILLION,
    UNIT_PARTS_PER_MILLION,
    UNIT_PERCENT,
)

from . import Reading, device_schema, get_device

DEPENDENCIES = ["bresser"]

UNIT_UV_INDEX = "UV index"


def _measurement(unit=None, device_class=None, decimals=0, **kwargs):
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    if device_class is not None:
        kwargs["device_class"] = device_class
    kwargs.setdefault("state_class", STATE_CLASS_MEASUREMENT)
    return sensor.sensor_schema(accuracy_decimals=decimals, **kwargs)


_temperature = _measurement(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, 1)
_wind = _measurement(UNIT_METER_PER_SECOND, DEVICE_CLASS_WIND_SPEED, 1)

# YAML key -> (schema, Reading member). Special-cased keys have no member.
FIELDS = {
    "temperature": (_temperature, "temperature"),
    "humidity": (_measurement(UNIT_PERCENT, DEVICE_CLASS_HUMIDITY), "humidity"),
    # total_increasing: the counter restarts at zero after a battery change.
    "rain": (
        _measurement(
            UNIT_MILLIMETER,
            DEVICE_CLASS_PRECIPITATION,
            1,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        "rain",
    ),
    "wind_speed": (_wind, "wind_speed"),
    "wind_gust": (_wind, "wind_gust"),
    "wind_direction": (
        _measurement(
            UNIT_DEGREES,
            DEVICE_CLASS_WIND_DIRECTION,
            state_class=STATE_CLASS_MEASUREMENT_ANGLE,
        ),
        "wind_direction",
    ),
    "illuminance": (_measurement(UNIT_LUX, DEVICE_CLASS_ILLUMINANCE), "illuminance_lux"),
    "uv_index": (_measurement(UNIT_UV_INDEX, decimals=1), "uv_index"),
    "globe_temperature": (_temperature, "globe_temperature"),
    "pm2_5": (_measurement(UNIT_MICROGRAMS_PER_CUBIC_METER, DEVICE_CLASS_PM25), "pm2_5"),
    "pm10": (_measurement(UNIT_MICROGRAMS_PER_CUBIC_METER, DEVICE_CLASS_PM10), "pm10"),
    "co2": (_measurement(UNIT_PARTS_PER_MILLION, DEVICE_CLASS_CARBON_DIOXIDE), "co2"),
    "hcho": (
        _measurement(UNIT_PARTS_PER_BILLION, DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS),
        "hcho",
    ),
    "voc_level": (_measurement(), "voc_level"),
    "channel": (
        sensor.sensor_schema(accuracy_decimals=0, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        None,
    ),
    "rssi": (
        _measurement(
            UNIT_DECIBEL_MILLIWATT,
            DEVICE_CLASS_SIGNAL_STRENGTH,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        None,
    ),
}

CONFIG_SCHEMA = device_schema({key: schema for key, (schema, _) in FIELDS.items()})


async def to_code(config):
    device = await get_device(config)
    for key, (_, member) in FIELDS.items():
        if key not in config:
            continue
        var = await sensor.new_sensor(config[key])
        if key == "channel":
            cg.add(device.set_channel_sensor(var))
        elif key == "rssi":
            cg.add(device.set_rssi_sensor(var))
        else:
            cg.add(device.add_sensor(cg.RawExpression(f"&{Reading}::{member}"), var))
