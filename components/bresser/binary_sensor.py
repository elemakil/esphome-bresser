import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_TIMEOUT,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import device_schema, get_device

DEPENDENCIES = ["bresser"]

CONFIG_SCHEMA = device_schema(
    {
        # device_class battery: ON means the battery is low.
        "battery_low": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_BATTERY),
        "startup": binary_sensor.binary_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        "online": binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ).extend(
            {
                # Kept below 2^32 ms (49.7 days) with room to spare, as the component stores ms.
                cv.Optional(CONF_TIMEOUT, default="2min"): cv.All(
                    cv.positive_not_null_time_period,
                    cv.Range(max=cv.TimePeriod(days=24)),
                ),
            }
        ),
    }
)


async def to_code(config):
    device = await get_device(config)
    if conf := config.get("battery_low"):
        cg.add(device.set_battery_sensor(await binary_sensor.new_binary_sensor(conf)))
    if conf := config.get("startup"):
        cg.add(device.set_startup_sensor(await binary_sensor.new_binary_sensor(conf)))
    if conf := config.get("online"):
        var = await binary_sensor.new_binary_sensor(conf)
        cg.add(device.set_online_sensor(var, conf[CONF_TIMEOUT].total_milliseconds))
