"""BL60X platform configuration boundaries."""

import pytest

from esphome.components import bl60x, libretiny
import esphome.config_validation as cv
from esphome.const import CONF_BOARD, CONF_FRAMEWORK, CONF_SOURCE, CONF_VERSION
from esphome.core import CORE, KEY_CORE
from esphome.types import ConfigType


def configure_bl60x(framework: ConfigType | None = None) -> ConfigType:
    CORE.data[KEY_CORE] = {}
    config = {CONF_BOARD: "generic-bl602"}
    if framework is not None:
        config[CONF_FRAMEWORK] = framework
    return bl60x.CONFIG_SCHEMA(config)


def test_default_framework_includes_bl602_platform() -> None:
    config = configure_bl60x()
    source = config[CONF_FRAMEWORK][CONF_SOURCE]
    assert source.startswith("https://github.com/Xmister/libretuya.git#")
    assert len(source.rsplit("#", 1)[1]) == 40


def test_explicit_framework_source_is_preserved() -> None:
    source = "https://example.com/custom-platform.git#custom"
    config = configure_bl60x({CONF_VERSION: "1.14.0", CONF_SOURCE: source})
    assert config[CONF_FRAMEWORK][CONF_SOURCE] == source


def test_generic_pins_match_arduino_variant() -> None:
    configure_bl60x()
    assert (
        libretiny.gpio.BASE_PIN_SCHEMA({"number": 16, "mode": {"output": True}})[
            "number"
        ]
        == 16
    )
    with pytest.raises(cv.Invalid, match="not usable"):
        libretiny.gpio.BASE_PIN_SCHEMA({"number": 22, "mode": {"output": True}})


def test_open_drain_output_is_rejected() -> None:
    configure_bl60x()
    with pytest.raises(cv.Invalid, match="open-drain"):
        libretiny.gpio.BASE_PIN_SCHEMA(
            {"number": 16, "mode": {"output": True, "open_drain": True}}
        )
