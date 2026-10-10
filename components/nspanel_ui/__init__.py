"""NSPanel custom UI: image-first pages drawn by the ESP32 on a resource-only Nextion TFT."""

import esphome.codegen as cg
from esphome.components import nextion, time
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = ["@LucaZiegler"]
DEPENDENCIES = ["display", "api"]
AUTO_LOAD = []

CONF_DISPLAY_ID = "display_id"
CONF_LIGHTS = "lights"
CONF_SCENES = "scenes"
CONF_QUICK_SCENES = "quick_scenes"
CONF_CLIMATE = "climate"
CONF_MEDIA_PLAYER = "media_player"
CONF_LOCK = "lock"
CONF_ALARM = "alarm"
CONF_DOOR_SCRIPT = "door_script"
CONF_TV_ON_SCRIPT = "tv_on_script"
CONF_SCREEN_OFF_SCRIPT = "screen_off_script"
CONF_OUT_DENON_SCRIPT = "out_denon_script"
CONF_OUT_TV_SCRIPT = "out_tv_script"
CONF_SCREENSAVER_TIMEOUT = "screensaver_timeout"

nspanel_ui_ns = cg.esphome_ns.namespace("nspanel_ui")
NSPanelUI = nspanel_ui_ns.class_("NSPanelUI", cg.Component)

# order matches enum Role in nspanel_ui.h
ROLES = [
    CONF_CLIMATE,
    CONF_MEDIA_PLAYER,
    CONF_LOCK,
    CONF_ALARM,
    CONF_DOOR_SCRIPT,
    CONF_TV_ON_SCRIPT,
    CONF_SCREEN_OFF_SCRIPT,
    CONF_OUT_DENON_SCRIPT,
    CONF_OUT_TV_SCRIPT,
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NSPanelUI),
        cv.Required(CONF_DISPLAY_ID): cv.use_id(nextion.Nextion),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_LIGHTS): cv.All(cv.ensure_list(cv.entity_id), cv.Length(min=12, max=12)),
        cv.Required(CONF_SCENES): cv.All(cv.ensure_list(cv.entity_id), cv.Length(min=6, max=6)),
        cv.Required(CONF_QUICK_SCENES): cv.All(cv.ensure_list(cv.entity_id), cv.Length(min=3, max=3)),
        **{cv.Required(role): cv.entity_id for role in ROLES},
        cv.Optional(CONF_SCREENSAVER_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(await cg.get_variable(config[CONF_DISPLAY_ID])))
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    for i, entity in enumerate(config[CONF_LIGHTS]):
        cg.add(var.set_light(i, entity))
    for i, entity in enumerate(config[CONF_SCENES]):
        cg.add(var.set_scene(i, entity))
    for i, entity in enumerate(config[CONF_QUICK_SCENES]):
        cg.add(var.set_quick(i, entity))
    for i, role in enumerate(ROLES):
        cg.add(var.set_role(i, config[role]))
    cg.add(var.set_screensaver_timeout(config[CONF_SCREENSAVER_TIMEOUT].total_milliseconds))
