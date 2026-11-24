import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DISTANCE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)

from . import CONF_KAIDI_DESK_CONTROLLER_ID, KaidiDeskController

DEPENDENCIES = ["kaidi_desk_controller"]

CONF_HEIGHT = "height"
CONF_RAW_POSITION = "raw_position"
CONF_STATUS = "status"
CONF_COLLISION_SENSITIVITY = "collision_sensitivity"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_KAIDI_DESK_CONTROLLER_ID): cv.use_id(KaidiDeskController),
        # Desk height in mm. To see inches or cm, change the display unit in Home Assistant.
        cv.Optional(CONF_HEIGHT): sensor.sensor_schema(
            unit_of_measurement="mm",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        # mm above the desk's lowest position
        cv.Optional(CONF_RAW_POSITION): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Byte 2 of the box's frames: 0 = normal, 48 = E04
        cv.Optional(CONF_STATUS): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Anti-collision level G-1..G-4, as shown in the handset's sensitivity menu
        cv.Optional(CONF_COLLISION_SENSITIVITY): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_KAIDI_DESK_CONTROLLER_ID])
    for key in (CONF_HEIGHT, CONF_RAW_POSITION, CONF_STATUS, CONF_COLLISION_SENSITIVITY):
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, f"set_{key}_sensor")(sens))
