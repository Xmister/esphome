from esphome.components.libretiny.const import FAMILY_BL60X

BL60X_BOARDS = {
    "generic-bl602": {"name": "Generic BL602 (experimental)", "family": FAMILY_BL60X},
    "sonoff-minir3": {
        "name": "Sonoff MINIR3 BL602 (experimental)",
        "family": FAMILY_BL60X,
    },
}
BL60X_BOARD_PINS = {
    "generic-bl602": {f"GPIO{i}": i for i in range(17)},
    # MINIR3 mappings: relay GPIO3 and status LED GPIO20 are documented by
    # OpenBeken; GPIO7/16 are the confirmed ROM ISP UART pads.
    "sonoff-minir3": {"GPIO3": 3, "GPIO7": 7, "GPIO16": 16, "GPIO20": 20},
}
BOARDS = BL60X_BOARDS
