"""
Tests for tools/crash_monitor.py -- crash dump parsing and address extraction.
"""

import sys
import os
import unittest
from unittest.mock import patch, MagicMock

# Add project root to path so we can import the tool
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools"))

import crash_monitor


SAMPLE_CRASH_DUMP = """\
=== CRASH DUMP BEGIN ===
Fault: HardFault
Thread: led (id=1)
Registers:
  PC  : 08000ABC
  LR  : 08000A34
  SP  : 200007B8
  R0  : 00000000
  R1  : 00000001
  R2  : 20000400
  R3  : 00000000
  R12 : 00000000
  xPSR: 61000000
Fault Status:
  CFSR : 00008200
  HFSR : 40000000
  MMFAR: 00000000
  BFAR : CCCCCCCC
Decoded:
  -> FORCED: Escalated to HardFault
  -> PRECISERR: Precise data bus error
  -> BFARVALID: BFAR = CCCCCCCC
Stack: base=20000400 size=00000400
EXC_RETURN: FFFFFFFD
=== CRASH DUMP END ===
"""


class TestRegisterExtraction(unittest.TestCase):
    """Test regex matching for register values in crash dump output."""

    def test_extract_pc(self):
        line = "  PC  : 08000ABC"
        match = crash_monitor.RE_REGISTER.match(line)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "PC")
        self.assertEqual(match.group(2), "08000ABC")

    def test_extract_lr(self):
        line = "  LR  : 08000A34"
        match = crash_monitor.RE_REGISTER.match(line)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "LR")
        self.assertEqual(match.group(2), "08000A34")

    def test_no_match_sp(self):
        """SP is not PC or LR, should not match."""
        line = "  SP  : 200007B8"
        match = crash_monitor.RE_REGISTER.match(line)
        self.assertIsNone(match)

    def test_no_match_cfsr(self):
        """Fault status registers should not match."""
        line = "  CFSR : 00008200"
        match = crash_monitor.RE_REGISTER.match(line)
        self.assertIsNone(match)

    def test_extract_from_full_dump(self):
        """Extract PC and LR from a full crash dump."""
        lines = SAMPLE_CRASH_DUMP.strip().split("\n")
        addresses = {}
        for line in lines:
            match = crash_monitor.RE_REGISTER.match(line)
            if match:
                addresses[match.group(1)] = match.group(2)

        self.assertIn("PC", addresses)
        self.assertIn("LR", addresses)
        self.assertEqual(addresses["PC"], "08000ABC")
        self.assertEqual(addresses["LR"], "08000A34")
        self.assertEqual(len(addresses), 2)  # Only PC and LR


class TestCrashMarkerDetection(unittest.TestCase):
    """Test crash dump begin/end marker detection."""

    def test_begin_marker(self):
        self.assertIn(crash_monitor.CRASH_BEGIN, "=== CRASH DUMP BEGIN ===")

    def test_end_marker(self):
        self.assertIn(crash_monitor.CRASH_END, "=== CRASH DUMP END ===")

    def test_begin_in_line(self):
        line = "\r\n=== CRASH DUMP BEGIN ===\r\n"
        self.assertIn(crash_monitor.CRASH_BEGIN, line)


class TestDecodeAddresses(unittest.TestCase):
    """Test addr2line invocation."""

    @patch("crash_monitor.subprocess.check_output")
    def test_decode_calls_addr2line(self, mock_check_output):
        """Verify addr2line is called with correct arguments."""
        mock_check_output.return_value = (
            "0x08000ABC\n"
            "ledThread(void*)\n"
            "/home/user/app/threads/main.cpp:42\n"
        )

        result = crash_monitor.decode_addresses(
            "build/app/threads/threads",
            ["08000ABC"]
        )

        mock_check_output.assert_called_once()
        args = mock_check_output.call_args[0][0]
        self.assertEqual(args[0], "arm-none-eabi-addr2line")
        self.assertIn("-fiaC", args)
        self.assertIn("-e", args)
        self.assertIn("build/app/threads/threads", args)
        self.assertIn("0x08000ABC", args)

    @patch("crash_monitor.subprocess.check_output")
    def test_decode_parses_output(self, mock_check_output):
        """Verify addr2line output is parsed correctly."""
        mock_check_output.return_value = (
            "0x08000ABC\n"
            "ledThread(void*)\n"
            "/home/user/app/threads/main.cpp:42\n"
        )

        result = crash_monitor.decode_addresses(
            "build/app/threads/threads",
            ["08000ABC"]
        )

        self.assertIn("08000ABC", result)
        self.assertIn("ledThread", result["08000ABC"])
        self.assertIn("main.cpp:42", result["08000ABC"])

    @patch("crash_monitor.subprocess.check_output")
    def test_decode_multiple_addresses(self, mock_check_output):
        """Verify multiple addresses are decoded."""
        mock_check_output.return_value = (
            "0x08000ABC\n"
            "ledThread(void*)\n"
            "/home/user/app/threads/main.cpp:42\n"
            "0x08000A34\n"
            "kernel::yield()\n"
            "/home/user/kernel/src/core/Kernel.cpp:102\n"
        )

        result = crash_monitor.decode_addresses(
            "build/app/threads/threads",
            ["08000ABC", "08000A34"]
        )

        self.assertEqual(len(result), 2)
        self.assertIn("08000ABC", result)
        self.assertIn("08000A34", result)

    def test_decode_empty_addresses(self):
        """Empty address list returns empty dict."""
        result = crash_monitor.decode_addresses("some.elf", [])
        self.assertEqual(result, {})

    def test_decode_no_elf(self):
        """No ELF path returns empty dict."""
        result = crash_monitor.decode_addresses(None, ["08000ABC"])
        self.assertEqual(result, {})

    @patch("crash_monitor.subprocess.check_output",
           side_effect=FileNotFoundError)
    def test_decode_addr2line_not_found(self, mock_check_output):
        """Missing addr2line returns empty dict."""
        result = crash_monitor.decode_addresses(
            "build/app/threads/threads",
            ["08000ABC"]
        )
        self.assertEqual(result, {})


class TestFlashAddressRegex(unittest.TestCase):
    """Test regex for matching FLASH addresses in general text."""

    def test_match_flash_address(self):
        matches = crash_monitor.RE_FLASH_ADDR.findall("PC is at 08000ABC")
        self.assertEqual(matches, ["08000ABC"])

    def test_no_match_sram_address(self):
        matches = crash_monitor.RE_FLASH_ADDR.findall("SP is at 200007B8")
        self.assertEqual(matches, [])

    def test_multiple_matches(self):
        text = "08000ABC and 08000A34 are in FLASH"
        matches = crash_monitor.RE_FLASH_ADDR.findall(text)
        self.assertEqual(len(matches), 2)


if __name__ == "__main__":
    unittest.main()
