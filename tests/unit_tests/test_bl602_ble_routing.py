from copy import deepcopy
import pytest
import voluptuous as vol
from esphome.config import _route_bl602_ble_tracker


def test_routes_tracker_and_nested_actions_without_changing_lambda():
    config = {
        "esp32_ble_tracker": {"on_ble_advertise": [{"then": [{"lambda": "esp32_ble_tracker.start_scan"}]}]},
        "interval": [{"then": [{"esp32_ble_tracker.start_scan": {}}, {"esp32_ble_tracker.stop_scan": {}}]}],
    }
    _route_bl602_ble_tracker(config)
    assert "esp32_ble_tracker" not in config
    assert config["bl602_ble_tracker"]["on_ble_advertise"][0]["then"][0]["lambda"] == "esp32_ble_tracker.start_scan"
    assert list(config["interval"][0]["then"][0]) == ["bl602_ble_tracker.start_scan"]
    assert list(config["interval"][0]["then"][1]) == ["bl602_ble_tracker.stop_scan"]


def test_no_tracker_is_noop():
    config = {"interval": [{"then": [{"esp32_ble_tracker.start_scan": {}}]}]}
    before = deepcopy(config)
    _route_bl602_ble_tracker(config)
    assert config == before


def test_conflicting_trackers_fail():
    config = {"esp32_ble_tracker": {}, "bl602_ble_tracker": {}}
    with pytest.raises(vol.Invalid, match="only one"):
        _route_bl602_ble_tracker(config)
