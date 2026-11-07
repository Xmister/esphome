"""BL602 GPIO capabilities."""

import esphome.config_validation as cv
from esphome.const import CONF_MODE, CONF_OPEN_DRAIN
from esphome.types import ConfigType


def validate_usage(config: ConfigType) -> ConfigType:
    """Reject open-drain output until the BL602 GPIO driver supports it."""
    if config[CONF_MODE][CONF_OPEN_DRAIN]:
        raise cv.Invalid(
            "BL602 does not support open-drain GPIO outputs",
            [CONF_MODE, CONF_OPEN_DRAIN],
        )
    return config
