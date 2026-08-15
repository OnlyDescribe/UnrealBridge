"""Focused stdlib tests for the standalone UnrealBridge preflight command."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
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
SPEC = importlib.util.spec_from_file_location("unreal_bridge_cli", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Cannot load bridge CLI module from {MODULE_PATH}")
bridge = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge
SPEC.loader.exec_module(bridge)


class PreflightCommandTests(unittest.TestCase):
    def run_preflight(self, lint_result, *, as_json=False):
        stdout = io.StringIO()
        stderr = io.StringIO()
        args = SimpleNamespace(file="-", json=as_json)
        with (
            mock.patch.object(bridge, "_preflight_or_skip", return_value=lint_result),
            mock.patch.object(sys, "stdin", io.StringIO("print('ok')")),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            exit_code = bridge.cmd_preflight(args)
        return exit_code, stdout.getvalue(), stderr.getvalue()

    def test_clean_result_returns_success(self):
        exit_code, stdout, stderr = self.run_preflight(([], []))

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.strip(), "preflight: clean")
        self.assertEqual(stderr, "")

    def test_errors_fail_and_warnings_are_reported_separately(self):
        exit_code, stdout, stderr = self.run_preflight(
            (["unknown bridge function"], ["raw API fallback"])
        )

        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout, "")
        self.assertIn("unknown bridge function", stderr)
        self.assertIn("raw API fallback", stderr)

    def test_json_result_preserves_warnings(self):
        exit_code, stdout, stderr = self.run_preflight(([], ["review fallback"]), as_json=True)

        self.assertEqual(exit_code, 0)
        self.assertEqual(
            json.loads(stdout),
            {"ok": True, "errors": [], "warnings": ["review fallback"]},
        )
        self.assertIn("review fallback", stderr)


if __name__ == "__main__":
    unittest.main()
