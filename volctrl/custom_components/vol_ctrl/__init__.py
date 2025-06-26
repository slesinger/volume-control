# custom_components/vol_ctrl/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.const import CONF_ID

# This is the most critical line for the C++ compiler.
# It ensures the 'spi' component's headers are included before this one.
DEPENDENCIES = ["spi"]

CODEOWNERS = ["@honza"]

vol_ctrl_ns = cg.esphome_ns.namespace('vol_ctrl')
VolCtrl = vol_ctrl_ns.class_('VolCtrl', cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(VolCtrl),
}).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=False))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add_library("Bodmer/TFT_eSPI", "^2.5.0")
    var.add_include("TFT_eSPI.h")
