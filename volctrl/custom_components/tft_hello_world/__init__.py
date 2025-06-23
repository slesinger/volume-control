# custom_components/tft_hello_world/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.const import CONF_ID

# This is the most critical line for the C++ compiler.
# It ensures the 'spi' component's headers are included before this one.
DEPENDENCIES = ["spi"]

CODEOWNERS = ["@honza"]

tft_hello_world_ns = cg.esphome_ns.namespace('tft_hello_world')
TFTHelloWorld = tft_hello_world_ns.class_('TFTHelloWorld', cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(TFTHelloWorld),
}).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=False))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add_library("Bodmer/TFT_eSPI", "^2.5.0")
    var.add_include("TFT_eSPI.h")
