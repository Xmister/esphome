from esphome.components.libretiny.const import FAMILY_BL60X

BL60X_BOARDS = {
    "generic-bl602": {"name": "Generic BL602 (experimental)", "family": FAMILY_BL60X},
    "sonoff-minir3": {
        "name": "Sonoff MINIR3 BL602 (experimental)",
        "family": FAMILY_BL60X,
    },
}
BL60X_BOARD_PINS = {
    "generic-bl602": {f"GPIO{i}": i for i in range(23)},
    # Expose the BL602 GPIO range for device-specific pin discovery.
    # On this MINIR3, GPIO1 is the status LED, GPIO5 the button, and GPIO22 the relay.
    "sonoff-minir3": {f"GPIO{i}": i for i in range(23)},
}
BOARDS = BL60X_BOARDS
