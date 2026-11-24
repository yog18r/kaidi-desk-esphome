import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_DISTANCE

from . import CONF_KAIDI_DESK_CONTROLLER_ID, KaidiDeskController, kaidi_desk_controller_ns

DEPENDENCIES = ["kaidi_desk_controller"]

DeskTargetNumber = kaidi_desk_controller_ns.class_("DeskTargetNumber", number.Number)

CONF_TARGET_HEIGHT = "target_height"

# The desk's lowest height is 711 mm, and the firmware accepts positions up to 460 mm above that.
MIN_HEIGHT_MM = 711
MAX_HEIGHT_MM = 1171

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_KAIDI_DESK_CONTROLLER_ID): cv.use_id(KaidiDeskController),
        # Set a height in mm and the desk drives there and stops
        cv.Optional(CONF_TARGET_HEIGHT): number.number_schema(
            DeskTargetNumber,
            unit_of_measurement="mm",
            device_class=DEVICE_CLASS_DISTANCE,
            icon="mdi:human-male-height",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_KAIDI_DESK_CONTROLLER_ID])
    if conf := config.get(CONF_TARGET_HEIGHT):
        num = await number.new_number(conf, min_value=MIN_HEIGHT_MM, max_value=MAX_HEIGHT_MM, step=1)
        cg.add(num.set_parent(parent))
