"""Desk Controller V3 for a Kaidi KDDY094 standing desk (see the repository README)."""

from esphome import pins
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@yog18r"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "button", "number"]

CONF_KAIDI_DESK_CONTROLLER_ID = "kaidi_desk_controller_id"
CONF_DESK_UART_ID = "desk_uart_id"
CONF_HANDSET_UART_ID = "handset_uart_id"
CONF_WAKE_PIN = "wake_pin"
CONF_HANDSET_SWITCH_PIN = "handset_switch_pin"
CONF_AWAKE_SENSE_PIN = "awake_sense_pin"

kaidi_desk_controller_ns = cg.esphome_ns.namespace("kaidi_desk_controller")
KaidiDeskController = kaidi_desk_controller_ns.class_("KaidiDeskController", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(KaidiDeskController),
        # UART to the control box: TX sends commands on T7, RX reads the height on T8.
        cv.Required(CONF_DESK_UART_ID): cv.use_id(uart.UARTComponent),
        # UART listening to the handset's side of the T7 switch (receive only).
        cv.Required(CONF_HANDSET_UART_ID): cv.use_id(uart.UARTComponent),
        # High raises T10 while the board uses the desk.
        cv.Required(CONF_WAKE_PIN): pins.gpio_output_pin_schema,
        # High opens the T7 switch, cutting the handset off from the box.
        cv.Required(CONF_HANDSET_SWITCH_PIN): pins.gpio_output_pin_schema,
        # Reads T10 through the divider. No internal pull: it would upset the divider.
        cv.Required(CONF_AWAKE_SENSE_PIN): pins.gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_desk_uart(await cg.get_variable(config[CONF_DESK_UART_ID])))
    cg.add(var.set_handset_uart(await cg.get_variable(config[CONF_HANDSET_UART_ID])))
    cg.add(var.set_wake_pin(await cg.gpio_pin_expression(config[CONF_WAKE_PIN])))
    cg.add(var.set_switch_pin(await cg.gpio_pin_expression(config[CONF_HANDSET_SWITCH_PIN])))
    cg.add(var.set_sense_pin(await cg.gpio_pin_expression(config[CONF_AWAKE_SENSE_PIN])))
