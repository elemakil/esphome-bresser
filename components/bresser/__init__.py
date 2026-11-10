"""Bresser 868 MHz weather and air quality sensors, received with an SX126x or CC1101."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_ID,
    CONF_MODEL,
    CONF_PLATFORM,
    CONF_SENSOR_ID,
    CONF_TRIGGER_ID,
)
from esphome.core import CORE

CODEOWNERS = ["@elemakil"]
# bresser.h uses all three entity types, whichever of them the configuration declares.
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

DOMAIN = "bresser"
CONF_BRESSER_ID = "bresser_id"
CONF_RADIO_ID = "radio_id"
CONF_ON_READING = "on_reading"

# Radio component domain -> the define that compiles its listener in bresser.h.
SUPPORTED_RADIOS = {
    "sx126x": "BRESSER_RADIO_SX126X",
    "cc1101": "BRESSER_RADIO_CC1101",
}

# Fields every model has, on top of its own.
COMMON_FIELDS = ("rssi", "battery_low", "startup", "online", "last_seen")
WEATHER = ("temperature", "humidity", "rain")
WIND_AND_LIGHT = ("wind_speed", "wind_gust", "wind_direction", "illuminance", "uv_index")

# model key -> (sensor type in the frame, model-specific fields)
MODELS = {
    "bresser_7in1": (1, WEATHER + WIND_AND_LIGHT),
    "bresser_8in1": (13, (*WEATHER, *WIND_AND_LIGHT, "globe_temperature")),
    "bresser_3in1": (12, WEATHER),
    "bresser_air_pm": (8, ("channel", "pm2_5", "pm10")),
    "bresser_co2": (10, ("channel", "co2")),
    "bresser_hchovoc": (11, ("channel", "hcho", "voc_level")),
}

bresser_ns = cg.esphome_ns.namespace("bresser")
BresserComponent = bresser_ns.class_("BresserComponent", cg.Component)
Reading = bresser_ns.struct("Reading")
ReadingTrigger = bresser_ns.class_(
    "ReadingTrigger", automation.Trigger.template(Reading.operator("const").operator("ref"))
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BresserComponent),
        # Any component id; _validate_radio checks it names a supported radio.
        cv.Required(CONF_RADIO_ID): cv.use_id(cg.Component),
        cv.Optional(CONF_ON_READING): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ReadingTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _radio_domain(full_config, radio_id):
    for domain in SUPPORTED_RADIOS:
        for block in full_config.get(domain) or []:
            if block.get(CONF_ID) == radio_id:
                return domain
    return None


def _validate_radio(config):
    if _radio_domain(fv.full_config.get(), config[CONF_RADIO_ID]) is None:
        raise cv.Invalid(
            f"'{config[CONF_RADIO_ID]}' is not a supported radio; "
            f"radio_id must name one of: {', '.join(SUPPORTED_RADIOS)}",
            path=[CONF_RADIO_ID],
        )


def _validate_sensor_ids(config):
    """A block without sensor_id matches every device of its model, so it would also take the
    frames of a device that another block names by id. Require all-or-nothing per model."""
    blocks = {}
    for domain in ("sensor", "binary_sensor", "text_sensor"):
        for block in fv.full_config.get().get(domain) or []:
            if block.get(CONF_PLATFORM) == DOMAIN and CONF_MODEL in block:
                blocks.setdefault(block[CONF_MODEL], []).append(block)
    for model, model_blocks in blocks.items():
        with_id = [b for b in model_blocks if CONF_SENSOR_ID in b]
        if with_id and len(with_id) != len(model_blocks):
            raise cv.Invalid(
                f"some '{model}' blocks set sensor_id and some do not; a block without "
                f"sensor_id would also receive the identified sensor's readings. "
                f"Set sensor_id on every '{model}' block."
            )


def _validate_one_unknown_devices(config):
    count = sum(
        "unknown_devices" in block
        for block in fv.full_config.get().get("text_sensor") or []
        if block.get(CONF_PLATFORM) == DOMAIN
    )
    if count > 1:
        raise cv.Invalid(
            f"'unknown_devices' is declared {count} times; the receiver has only one, "
            "so keep a single block with it."
        )


def _final_validate(config):
    _validate_radio(config)
    _validate_sensor_ids(config)
    _validate_one_unknown_devices(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    domain = _radio_domain(CORE.config, config[CONF_RADIO_ID])
    cg.add_define(SUPPORTED_RADIOS[domain])
    cg.add(var.set_radio(await cg.get_variable(config[CONF_RADIO_ID])))

    for conf in config.get(CONF_ON_READING, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(Reading.operator("const").operator("ref"), "x")], conf
        )


# --- Shared by the sensor, binary_sensor and text_sensor platforms -----------------------

FIELD_KEYS = {*COMMON_FIELDS, *(f for _, fields in MODELS.values() for f in fields)}


def _validate_device_block(config):
    model = config[CONF_MODEL]
    allowed = COMMON_FIELDS + MODELS[model][1]
    for key in config:
        if key in FIELD_KEYS and key not in allowed:
            raise cv.Invalid(f"'{key}' is not available for model '{model}'", path=[key])
    # The block's device_id applies to every entity that does not set its own.
    if CONF_DEVICE_ID in config:
        for key in config:
            if key in FIELD_KEYS:
                config[key].setdefault(CONF_DEVICE_ID, config[CONF_DEVICE_ID])
    return config


def device_schema(fields: dict):
    """Schema of one platform block: a device (model + optional sensor_id) and its entities."""
    return cv.All(
        cv.Schema(
            {
                cv.GenerateID(CONF_BRESSER_ID): cv.use_id(BresserComponent),
                cv.Required(CONF_MODEL): cv.one_of(*MODELS, lower=True),
                cv.Optional(CONF_SENSOR_ID): cv.int_range(min=0, max=0xFFFF),
                cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
                **{cv.Optional(key): schema for key, schema in fields.items()},
            }
        ),
        _validate_device_block,
    )


async def get_device(config):
    """The BresserDevice for a block. Blocks with the same model and sensor_id share one."""
    hub = await cg.get_variable(config[CONF_BRESSER_ID])
    s_type = MODELS[config[CONF_MODEL]][0]
    sensor_id = config.get(CONF_SENSOR_ID, -1)
    # get_device() returns a pointer, so member calls on the result must use `->`.
    return cg.MockObj(hub.get_device(s_type, sensor_id), "->")
