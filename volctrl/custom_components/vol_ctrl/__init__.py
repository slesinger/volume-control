# custom_components/vol_ctrl/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import spi, output
from esphome.const import CONF_ID, CONF_BACKLIGHT_PIN, CONF_NAME, CONF_TRIGGER_ID

# This is the most critical line for the C++ compiler.
# It ensures the 'spi' component's headers are included before this one.
DEPENDENCIES = ["spi"]
CODEOWNERS = ["@honza"]

# Configuration constants
CONF_SPI_ID = "spi_id"
CONF_MAX_VOLUME = "max_volume"
CONF_WIIM_IP = "wiim_ip"
CONF_ENCODER_PIN_A = "encoder_pin_a"
CONF_ENCODER_PIN_B = "encoder_pin_b"
CONF_QUICK_ACTIONS = "quick_actions"

vol_ctrl_ns = cg.esphome_ns.namespace('vol_ctrl')
VolCtrl = vol_ctrl_ns.class_('VolCtrl', cg.Component, spi.SPIDevice)

QUICK_ACTION_SCHEMA = automation.validate_automation(
    {
        cv.Required(CONF_NAME): cv.string,
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(automation.Trigger.template()),
    }
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(VolCtrl),
    cv.Optional(CONF_BACKLIGHT_PIN): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_MAX_VOLUME, default=120.0): cv.float_range(min=0.0, max=120.0),
    # Optional WiiM streamer: its IPv4 address, or "auto" to find it via SSDP. Omitted = WiiM features off.
    cv.Optional(CONF_WIIM_IP): cv.string,
    # Rotary encoder (decoded here with debouncing, no separate rotary_encoder sensor needed)
    cv.Required(CONF_ENCODER_PIN_A): pins.internal_gpio_input_pullup_pin_schema,
    cv.Required(CONF_ENCODER_PIN_B): pins.internal_gpio_input_pullup_pin_schema,
    # Entries of the "Home Assistant" menu: a name and the actions to run when it is picked
    cv.Optional(CONF_QUICK_ACTIONS): QUICK_ACTION_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=False))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    
    cg.add(var.set_max_volume(config[CONF_MAX_VOLUME]))

    if CONF_WIIM_IP in config:
        wiim_ip = config[CONF_WIIM_IP]
        cg.add(var.set_wiim_ip("" if wiim_ip == "auto" else wiim_ip))

    cg.add(var.set_encoder_pins(await cg.gpio_pin_expression(config[CONF_ENCODER_PIN_A]),
                                await cg.gpio_pin_expression(config[CONF_ENCODER_PIN_B])))

    for action in config.get(CONF_QUICK_ACTIONS, []):
        trigger = cg.new_Pvariable(action[CONF_TRIGGER_ID])
        await automation.build_automation(trigger, [], action)
        cg.add(var.add_quick_action(action[CONF_NAME], trigger))

    if CONF_BACKLIGHT_PIN in config:
        backlight = await cg.get_variable(config[CONF_BACKLIGHT_PIN])
        cg.add(var.set_backlight_pin(backlight))

    cg.add_library("Bodmer/TFT_eSPI", "^2.5.0")
    cg.add_library("Bodmer/TJpg_Decoder", "^1.1.0")
    for lib in ("FS", "SPIFFS", "LittleFS", "SD", "SPI"):  # TJpg_Decoder includes them unconditionally on ESP32
        cg.add_library(lib, None)
    var.add_include("TFT_eSPI.h")
