"""Focused stdlib tests for the focus-independent PIE mouse CLI surface."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import math
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "bridge.py"
)
MANIFEST_PATH = MODULE_PATH.with_name("bridge_manifest.json")
SPEC = importlib.util.spec_from_file_location("unreal_bridge_pie_mouse_cli", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Cannot load bridge CLI module from {MODULE_PATH}")
bridge = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge
SPEC.loader.exec_module(bridge)


class PIEMouseCommandTests(unittest.TestCase):
    @staticmethod
    def args(command: str, **overrides):
        values = {
            "pie_mouse_command": command,
            "json": False,
            "delta_x": 0.0,
            "delta_y": 0.0,
            "delta": 0.0,
            "button": "left",
            "button_action": "click",
        }
        values.update(overrides)
        return SimpleNamespace(**values)

    def capture_execute(self, args):
        with mock.patch.object(bridge, "_execute", return_value=17) as execute:
            exit_code = bridge.cmd_pie_mouse(args)
        self.assertEqual(exit_code, 17)
        return execute.call_args

    def test_move_targets_gameplay_library_and_preserves_signed_deltas(self):
        call = self.capture_execute(
            self.args("move", delta_x=12.5, delta_y=-3.25)
        )

        code = call.args[1]
        self.assertIn("send_pie_mouse_move(12.5, -3.25)", code)
        self.assertIn('"diagnostic_code"', code)
        self.assertIn("raise RuntimeError", code)
        self.assertNotIn("Slate", code)
        self.assertNotIn("ctypes", code)
        self.assertEqual(call.kwargs["mode"], "pie-mouse-move")

    def test_button_actions_use_supported_reflected_enum(self):
        expected_calls = {
            "press": "send_pie_mouse_button(unreal.BridgePIEMouseButton.RIGHT, True)",
            "release": "send_pie_mouse_button(unreal.BridgePIEMouseButton.RIGHT, False)",
            "click": "click_pie_mouse_button(unreal.BridgePIEMouseButton.RIGHT)",
        }
        for action, expected in expected_calls.items():
            with self.subTest(action=action):
                call = self.capture_execute(
                    self.args("button", button="right", button_action=action)
                )
                self.assertIn(expected, call.args[1])
                self.assertEqual(call.kwargs["mode"], f"pie-mouse-button-{action}")

    def test_wheel_and_release_all_have_dedicated_calls(self):
        wheel_call = self.capture_execute(self.args("wheel", delta=-2.0))
        self.assertIn("send_pie_mouse_wheel(-2.0)", wheel_call.args[1])

        release_call = self.capture_execute(self.args("release-all"))
        self.assertIn("release_all_pie_mouse_buttons()", release_call.args[1])

    def test_state_serializes_readiness_and_press_ownership(self):
        call = self.capture_execute(self.args("state"))
        code = call.args[1]
        self.assertIn("get_pie_mouse_input_state()", code)
        self.assertIn('"tracked_pressed_buttons"', code)
        self.assertIn('"pending_click_release_buttons"', code)
        self.assertNotIn("raise RuntimeError", code)

    def test_generated_call_passes_current_manifest_preflight(self):
        code = bridge._pie_mouse_result_code(
            "unreal.UnrealBridgeGameplayLibrary.send_pie_mouse_button("
            "unreal.BridgePIEMouseButton.MIDDLE, True)"
        )
        errors, _warnings = bridge._preflight_or_skip(code)
        self.assertEqual(errors, [])

    def test_manifest_lists_mouse_capabilities_and_return_structs(self):
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        functions = manifest["libraries"]["UnrealBridgeGameplayLibrary"]["functions"]
        self.assertTrue({
            "send_pie_mouse_move",
            "send_pie_mouse_button",
            "click_pie_mouse_button",
            "send_pie_mouse_wheel",
            "release_all_pie_mouse_buttons",
            "get_pie_mouse_input_state",
        }.issubset(functions))
        self.assertEqual(
            set(manifest["enums"]["BridgePIEMouseButton"]),
            {"LEFT", "RIGHT", "MIDDLE"},
        )
        self.assertIn("diagnostic_code", manifest["structs"]["BridgePIEMouseInputResult"])
        self.assertIn("player_input_pressed_buttons", manifest["structs"]["BridgePIEMouseInputState"])

    def test_non_finite_and_zero_deltas_fail_locally(self):
        for args in (
            self.args("move", delta_x=math.nan, delta_y=1.0),
            self.args("move", delta_x=0.0, delta_y=0.0),
            self.args("wheel", delta=math.inf),
            self.args("wheel", delta=0.0),
        ):
            with self.subTest(args=args):
                stdout = io.StringIO()
                args.json = True
                with (
                    mock.patch.object(bridge, "_execute") as execute,
                    contextlib.redirect_stdout(stdout),
                ):
                    exit_code = bridge.cmd_pie_mouse(args)
                self.assertEqual(exit_code, 2)
                execute.assert_not_called()
                payload = json.loads(stdout.getvalue())
                self.assertFalse(payload["success"])
                self.assertEqual(payload["diagnostic_code"], "invalid_argument")


if __name__ == "__main__":
    unittest.main()
