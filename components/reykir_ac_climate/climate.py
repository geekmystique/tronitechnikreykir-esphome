import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, switch, select
from esphome.const import ENTITY_CATEGORY_CONFIG

CODEOWNERS = ["@you"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["switch", "select"]

reykir_ac_climate_ns = cg.esphome_ns.namespace("reykir_ac_climate")
ReykirAcClimate = reykir_ac_climate_ns.class_(
    "ReykirAcClimate", climate.Climate, cg.PollingComponent, uart.UARTDevice
)

ReykirAcSleepSwitch = reykir_ac_climate_ns.class_("ReykirAcSleepSwitch", switch.Switch, cg.Component)
ReykirAcUvcSwitch = reykir_ac_climate_ns.class_("ReykirAcUvcSwitch", switch.Switch, cg.Component)
ReykirAcMuteSwitch = reykir_ac_climate_ns.class_("ReykirAcMuteSwitch", switch.Switch, cg.Component)
ReykirAcDisplaySwitch = reykir_ac_climate_ns.class_("ReykirAcDisplaySwitch", switch.Switch, cg.Component)
ReykirAcVaneSelect = reykir_ac_climate_ns.class_("ReykirAcVaneSelect", select.Select, cg.Component)

CONF_SLEEP_MODE = "sleep_mode"
CONF_UVC_LIGHT = "uvc_light"
CONF_MUTE = "mute"
CONF_DISPLAY = "display"
CONF_VANE_POSITION = "vane_position"

# Options are ordered to match byte values 0x00-0x05 exactly (index == byte
# value), which the C++ side relies on - keep this list and the vane byte
# encoding in sync if it's ever changed.
VANE_OPTIONS = ["Swing", "Position 1", "Position 2", "Position 3", "Position 4", "Position 5 (Top)"]

CONFIG_SCHEMA = climate.climate_schema(ReykirAcClimate).extend(
    {
        cv.Optional(CONF_SLEEP_MODE): switch.switch_schema(
            ReykirAcSleepSwitch, entity_category=ENTITY_CATEGORY_CONFIG
        ),
        cv.Optional(CONF_UVC_LIGHT): switch.switch_schema(
            ReykirAcUvcSwitch, entity_category=ENTITY_CATEGORY_CONFIG
        ),
        cv.Optional(CONF_MUTE): switch.switch_schema(
            ReykirAcMuteSwitch, entity_category=ENTITY_CATEGORY_CONFIG
        ),
        cv.Optional(CONF_DISPLAY): switch.switch_schema(
            ReykirAcDisplaySwitch, entity_category=ENTITY_CATEGORY_CONFIG
        ),
        cv.Optional(CONF_VANE_POSITION): select.select_schema(
            ReykirAcVaneSelect, entity_category=ENTITY_CATEGORY_CONFIG
        ),
    }
).extend(uart.UART_DEVICE_SCHEMA).extend(cv.polling_component_schema("5s"))


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_SLEEP_MODE in config:
        sw = await switch.new_switch(config[CONF_SLEEP_MODE])
        await cg.register_component(sw, config[CONF_SLEEP_MODE])
        cg.add(sw.set_parent(var))
        cg.add(var.set_sleep_switch(sw))

    if CONF_UVC_LIGHT in config:
        sw = await switch.new_switch(config[CONF_UVC_LIGHT])
        await cg.register_component(sw, config[CONF_UVC_LIGHT])
        cg.add(sw.set_parent(var))
        cg.add(var.set_uvc_switch(sw))

    if CONF_MUTE in config:
        sw = await switch.new_switch(config[CONF_MUTE])
        await cg.register_component(sw, config[CONF_MUTE])
        cg.add(sw.set_parent(var))
        cg.add(var.set_mute_switch(sw))

    if CONF_DISPLAY in config:
        sw = await switch.new_switch(config[CONF_DISPLAY])
        await cg.register_component(sw, config[CONF_DISPLAY])
        cg.add(sw.set_parent(var))
        cg.add(var.set_display_switch(sw))

    if CONF_VANE_POSITION in config:
        sel = await select.new_select(config[CONF_VANE_POSITION], options=VANE_OPTIONS)
        await cg.register_component(sel, config[CONF_VANE_POSITION])
        cg.add(sel.set_parent(var))
        cg.add(var.set_vane_select(sel))
