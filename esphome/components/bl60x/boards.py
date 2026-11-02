from esphome.components.libretiny.const import FAMILY_BL60X

BL60X_BOARDS = {
    "generic-bl602": {"name": "Generic BL602 (experimental)", "family": FAMILY_BL60X},
    "sonoff-minir3": {"name": "Sonoff MINIR3 BL602 (experimental)", "family": FAMILY_BL60X},
}
BL60X_BOARD_PINS = {
    "generic-bl602": {f"GPIO{i}": i for i in range(17)},
    # Expose only the confirmed ROM ISP UART pads. The relay/control GPIO map
    # has not been identified, so a MINIR3 config cannot assign it by mistake.
    "sonoff-minir3": {"GPIO7": 7, "GPIO16": 16},
}
BOARDS = BL60X_BOARDS
