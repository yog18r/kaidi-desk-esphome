import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_PROBLEM, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_KAIDI_DESK_CONTROLLER_ID, KaidiDeskController

DEPENDENCIES = ["kaidi_desk_controller"]

CONF_MOVING = "moving"
CONF_HANDSET_IN_USE = "handset_in_use"
CONF_HANDSET_NOT_RESPONDING = "handset_not_responding"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_KAIDI_DESK_CONTROLLER_ID): cv.use_id(KaidiDeskController),
        # On while the desk is moving, from Home Assistant or the handset
        cv.Optional(CONF_MOVING): binary_sensor.binary_sensor_schema(),
        # On while the handset is awake (its screen is lit)
        cv.Optional(CONF_HANDSET_IN_USE): binary_sensor.binary_sensor_schema(),
        # On when the handset is awake but has sent nothing for about 5 s
        cv.Optional(CONF_HANDSET_NOT_RESPONDING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_KAIDI_DESK_CONTROLLER_ID])
    for key in (CONF_MOVING, CONF_HANDSET_IN_USE, CONF_HANDSET_NOT_RESPONDING):
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(parent, f"set_{key}_binary_sensor")(sens))
