import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv

from . import CONF_KAIDI_DESK_CONTROLLER_ID, KaidiDeskController, kaidi_desk_controller_ns

DEPENDENCIES = ["kaidi_desk_controller"]

DeskButton = kaidi_desk_controller_ns.class_("DeskButton", button.Button)

# key: (the action the firmware runs, default icon). 1 up, 2 down, 0 stop, 6-9 go to preset 1-4.
BUTTONS = {
    "up": (1, "mdi:arrow-up"),
    "down": (2, "mdi:arrow-down"),
    "stop": (0, "mdi:stop"),
    "preset_1": (6, "mdi:numeric-1-box-outline"),
    "preset_2": (7, "mdi:numeric-2-box-outline"),
    "preset_3": (8, "mdi:numeric-3-box-outline"),
    "preset_4": (9, "mdi:numeric-4-box-outline"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_KAIDI_DESK_CONTROLLER_ID): cv.use_id(KaidiDeskController),
        **{
            cv.Optional(key): button.button_schema(DeskButton, icon=icon)
            for key, (_, icon) in BUTTONS.items()
        },
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_KAIDI_DESK_CONTROLLER_ID])
    for key, (action, _) in BUTTONS.items():
        if conf := config.get(key):
            btn = await button.new_button(conf)
            cg.add(btn.set_parent(parent))
            cg.add(btn.set_action(action))
