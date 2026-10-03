# custom_components/vol_ctrl/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi, output
from esphome.const import CONF_ID, CONF_BACKLIGHT_PIN

# This is the most critical line for the C++ compiler.
# It ensures the 'spi' component's headers are included before this one.
DEPENDENCIES = ["spi"]
CODEOWNERS = ["@honza"]

# Configuration constants
CONF_SPI_ID = "spi_id"
CONF_MAX_VOLUME = "max_volume"
CONF_WIIM_IP = "wiim_ip"

vol_ctrl_ns = cg.esphome_ns.namespace('vol_ctrl')
VolCtrl = vol_ctrl_ns.class_('VolCtrl', cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(VolCtrl),
    cv.Optional(CONF_BACKLIGHT_PIN): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_MAX_VOLUME, default=120.0): cv.float_range(min=0.0, max=120.0),
    # Optional WiiM streamer: its IPv4 address, or "auto" to find it via SSDP. Omitted = WiiM features off.
    cv.Optional(CONF_WIIM_IP): cv.string,
}).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=False))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    
    cg.add(var.set_max_volume(config[CONF_MAX_VOLUME]))

    if CONF_WIIM_IP in config:
        wiim_ip = config[CONF_WIIM_IP]
        cg.add(var.set_wiim_ip("" if wiim_ip == "auto" else wiim_ip))

    if CONF_BACKLIGHT_PIN in config:
        backlight = await cg.get_variable(config[CONF_BACKLIGHT_PIN])
        cg.add(var.set_backlight_pin(backlight))

    cg.add_library("Bodmer/TFT_eSPI", "^2.5.0")
    var.add_include("TFT_eSPI.h")
