import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_MODEL,
    CONF_SENSOR_ID,
    DEVICE_CLASS_TIMESTAMP,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import (
    CONF_BRESSER_ID,
    BresserComponent,
    device_schema,
    get_device,
)

DEPENDENCIES = ["bresser"]

CONF_LAST_SEEN = "last_seen"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_UNKNOWN_DEVICES = "unknown_devices"

DEVICE_SCHEMA = device_schema(
    {
        CONF_LAST_SEEN: text_sensor.text_sensor_schema(
            device_class=DEVICE_CLASS_TIMESTAMP,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # The id received in the frame, i.e. the value to use for `sensor_id`. A text sensor
        # because it is an identifier: shown as-is, never graphed or averaged.
        CONF_TRANSMITTER_ID: text_sensor.text_sensor_schema(
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)

# A receiver-level entity, not tied to any one sensor.
HUB_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BRESSER_ID): cv.use_id(BresserComponent),
        cv.Required(CONF_UNKNOWN_DEVICES): text_sensor.text_sensor_schema(
            icon="mdi:radio-tower",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


def CONFIG_SCHEMA(config):
    if CONF_UNKNOWN_DEVICES not in config:
        return DEVICE_SCHEMA(config)
    for key in (CONF_MODEL, CONF_SENSOR_ID, CONF_DEVICE_ID):
        if key in config:
            raise cv.Invalid(
                f"'{key}' cannot be used in the block with '{CONF_UNKNOWN_DEVICES}': "
                "that entity belongs to the receiver, not to a sensor. "
                "Put it in a block of its own.",
                path=[key],
            )
    return HUB_SCHEMA(config)


async def to_code(config):
    if conf := config.get(CONF_UNKNOWN_DEVICES):
        hub = await cg.get_variable(config[CONF_BRESSER_ID])
        cg.add(hub.set_unknown_devices_sensor(await text_sensor.new_text_sensor(conf)))
        return
    device = await get_device(config)
    if conf := config.get(CONF_LAST_SEEN):
        cg.add(device.set_last_seen_sensor(await text_sensor.new_text_sensor(conf)))
    if conf := config.get(CONF_TRANSMITTER_ID):
        cg.add(device.set_transmitter_id_sensor(await text_sensor.new_text_sensor(conf)))
