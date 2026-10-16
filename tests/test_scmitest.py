#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise scmitest against temporary IIO attributes without hardware."""

import ctypes
import errno
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


LONG_MAX = (1 << (ctypes.sizeof(ctypes.c_long) * 8 - 1)) - 1
LONG_MIN = -LONG_MAX - 1


class ScmiTest(unittest.TestCase):
    binary = None

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="scmitest-iio-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def sensor(self, index=0, name="scmi-smoke", values=("0", "101", "202")):
        device = self.root / f"iio:device{index}"
        device.mkdir()
        if name is not None:
            (device / "name").write_text(name + "\n", encoding="utf-8")
        for axis, value in zip("xyz", values):
            (device / f"in_accel_{axis}_raw").write_text(value + "\n", encoding="utf-8")
        return device

    def run_tool(self, *arguments, expected=0):
        result = subprocess.run(
            [str(self.binary), "--root", str(self.root), *arguments],
            capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_help(self):
        for option in ("--help", "-h"):
            with self.subTest(option=option):
                result = self.run_tool(option)
                self.assertIn("Default expected name: scmi-smoke", result.stdout)
                self.assertEqual(result.stderr, "")

    def test_invalid_arguments(self):
        for arguments in (("--root",), ("first", "second"), ("--list", "selected")):
            with self.subTest(arguments=arguments):
                result = self.run_tool(*arguments, expected=2)
                self.assertIn("Usage:", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_default_sensor_and_pause(self):
        device = self.sensor()
        start = time.monotonic()
        result = self.run_tool()
        self.assertGreaterEqual(time.monotonic() - start, 0.09)
        self.assertIn(f"device={device} name=scmi-smoke\n", result.stdout)
        self.assertIn("sample0 x=0 y=101 z=202\n", result.stdout)
        self.assertIn("sample1 x=0 y=101 z=202\n", result.stdout)
        self.assertIn("delta x=0 y=0 z=0\n", result.stdout)
        self.assertTrue(result.stdout.endswith("scmitest: PASS\n"))
        self.assertEqual(result.stderr, "")

    def test_named_sensor_selection(self):
        self.sensor(0, "other")
        selected = self.sensor(1, "selected", ("4", "5", "6"))
        result = self.run_tool("selected")
        self.assertIn(f"device={selected} name=selected\n", result.stdout)
        self.assertIn("sample0 x=4 y=5 z=6\n", result.stdout)

    def test_default_sensor_selection(self):
        self.sensor(0, "other")
        selected = self.sensor(1)
        self.assertIn(f"device={selected} ", self.run_tool().stdout)

    def test_missing_name_fails(self):
        self.sensor(name="other")
        result = self.run_tool("absent", expected=1)
        self.assertIn("device named absent", result.stderr)
        self.assertNotIn("PASS", result.stdout)

    def test_incomplete_sensor_is_skipped(self):
        incomplete = self.sensor()
        (incomplete / "in_accel_z_raw").unlink()
        selected = self.sensor(1)
        self.assertIn(f"device={selected} ", self.run_tool().stdout)

    def test_missing_device_name(self):
        self.sensor(name=None)
        self.assertIn("name=<unknown>", self.run_tool("--list").stdout)

    def test_first_line_only(self):
        device = self.sensor(name="scmi-smoke\nignored")
        (device / "in_accel_x_raw").write_text("7\ninvalid\n", encoding="utf-8")
        result = self.run_tool()
        self.assertIn("name=scmi-smoke\n", result.stdout)
        self.assertIn("sample0 x=7 ", result.stdout)

    def test_non_iio_directory_is_ignored(self):
        self.sensor().rename(self.root / "other-device")
        self.run_tool(expected=1)

    def test_symlink_device(self):
        target = self.sensor().rename(self.root / "sensor-target")
        device = self.root / "iio:device1"
        device.symlink_to(target, target_is_directory=True)
        (self.root / "iio:device2").symlink_to(self.root / "absent")
        self.assertIn(f"device={device} ", self.run_tool().stdout)

    def test_list_reports_attributes_without_sampling(self):
        self.sensor()
        incomplete = self.sensor(1, "incomplete")
        (incomplete / "in_accel_y_raw").unlink()
        result = self.run_tool("--list")
        self.assertIn("name=scmi-smoke accel_triplet=yes", result.stdout)
        self.assertIn("name=incomplete accel_triplet=no", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_empty_missing_and_non_directory_roots(self):
        self.run_tool(expected=1)
        self.run_tool("--list")
        for root in (self.root / "absent", self.root / "file"):
            if root.name == "file":
                root.write_text("not a directory", encoding="utf-8")
            with self.subTest(root=root):
                self.run_tool("--root", str(root), expected=1)
                self.run_tool("--root", str(root), "--list")

    def test_numeric_compatibility(self):
        device = self.sensor()
        attribute = device / "in_accel_x_raw"
        for text, value in (("-12", -12), (" \t+42\r", 42), ("012", 12),
                            ("12suffix", 12), (str(LONG_MIN), LONG_MIN),
                            (str(LONG_MAX), LONG_MAX)):
            with self.subTest(text=text):
                attribute.write_text(text + "\n", encoding="utf-8")
                self.assertIn(f"sample0 x={value} ", self.run_tool().stdout)

    def test_invalid_and_empty_values_fail(self):
        device = self.sensor()
        attribute = device / "in_accel_x_raw"
        for text in ("", "\n", " \t\n", "invalid\n", "+\n", "+-1\n", "++1\n",
                     f"{LONG_MAX + 1}\n", f"{LONG_MIN - 1}\n"):
            with self.subTest(text=text):
                attribute.write_text(text, encoding="utf-8")
                result = self.run_tool(expected=1)
                self.assertIn("failed to read first sample", result.stderr)
                if not text:
                    self.assertIn(os.strerror(errno.EIO), result.stderr)
                self.assertNotIn("PASS", result.stdout)

    @unittest.skipIf(os.geteuid() == 0, "read permission test needs a non-root user")
    def test_unreadable_sensor_is_skipped(self):
        unreadable = self.sensor() / "in_accel_x_raw"
        unreadable.chmod(0)
        self.addCleanup(unreadable.chmod, 0o600)
        selected = self.sensor(1)
        self.assertIn(f"device={selected} ", self.run_tool().stdout)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: test_scmitest.py SCMITEST_BINARY")
    ScmiTest.binary = Path(sys.argv.pop()).resolve()
    unittest.main(verbosity=2)
