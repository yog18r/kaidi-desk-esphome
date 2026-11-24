import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import CONF_KAIDI_DESK_CONTROLLER_ID, KaidiDeskController

DEPENDENCIES = ["kaidi_desk_controller"]

CONF_LAST_MOVE_RESULT = "last_move_result"
CONF_HANDSET_BUTTON = "handset_button"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_KAIDI_DESK_CONTROLLER_ID): cv.use_id(KaidiDeskController),
        # Why the last Home Assistant move ended
        cv.Optional(CONF_LAST_MOVE_RESULT): text_sensor.text_sensor_schema(),
        # What the handset is sending right now
        cv.Optional(CONF_HANDSET_BUTTON): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_KAIDI_DESK_CONTROLLER_ID])
    for key in (CONF_LAST_MOVE_RESULT, CONF_HANDSET_BUTTON):
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(parent, f"set_{key}_text_sensor")(sens))
