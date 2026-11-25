import esphome.codegen as cg
from esphome.components import light, output
import esphome.config_validation as cv
from esphome.const import CONF_BLUE, CONF_GREEN, CONF_OUTPUT_ID, CONF_RED

from . import colorshadow_ns

ColorShadowLightOutput = colorshadow_ns.class_(
    "ColorShadowLightOutput", light.LightOutput
)

# The same options as the rgb platform; only the transitions differ.
CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(ColorShadowLightOutput),
        cv.Required(CONF_RED): cv.use_id(output.FloatOutput),
        cv.Required(CONF_GREEN): cv.use_id(output.FloatOutput),
        cv.Required(CONF_BLUE): cv.use_id(output.FloatOutput),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    cg.add(var.set_red(await cg.get_variable(config[CONF_RED])))
    cg.add(var.set_green(await cg.get_variable(config[CONF_GREEN])))
    cg.add(var.set_blue(await cg.get_variable(config[CONF_BLUE])))
