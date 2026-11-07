"""Experimental BL602 BLE controller. Requires the LibreTiny BL602 family."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

CODEOWNERS = ["@Xmister"]
DEPENDENCIES = ["bl60x"]
DOMAIN = "bl602_ble"

bl602_ble_ns = cg.esphome_ns.namespace("bl602_ble")
BL602BLE = bl602_ble_ns.class_("BL602BLE", cg.Component)
CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(BL602BLE)}).extend(
    cv.COMPONENT_SCHEMA
)
request_scan_listener_slot = cg.slot_counter("BL602_BLE_SCAN_LISTENER_COUNT")


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add_define("USE_BL602_BLE")
