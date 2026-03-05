"""Tests for tools/hw_driver_runner.py."""

import os
import sys
import unittest

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools"))

import hw_driver_runner


class ParseMachineLineTest(unittest.TestCase):
    def test_parse_case_pass(self):
        line = "MSOS_CASE:uart:single-byte:PASS"
        parsed = hw_driver_runner.parse_machine_line(line)
        self.assertIsInstance(parsed, hw_driver_runner.CaseResult)
        self.assertEqual(parsed.driver, "uart")
        self.assertEqual(parsed.case_name, "single-byte")
        self.assertTrue(parsed.passed)

    def test_parse_case_fail(self):
        line = "MSOS_CASE:spi:burst:FAIL"
        parsed = hw_driver_runner.parse_machine_line(line)
        self.assertIsInstance(parsed, hw_driver_runner.CaseResult)
        self.assertFalse(parsed.passed)

    def test_parse_summary(self):
        line = "MSOS_SUMMARY:i2c:pass=7:total=8:result=FAIL"
        parsed = hw_driver_runner.parse_machine_line(line)
        self.assertIsInstance(parsed, hw_driver_runner.SummaryResult)
        self.assertEqual(parsed.driver, "i2c")
        self.assertEqual(parsed.pass_count, 7)
        self.assertEqual(parsed.total_count, 8)
        self.assertFalse(parsed.passed)

    def test_parse_ignores_non_machine_line(self):
        parsed = hw_driver_runner.parse_machine_line("hello")
        self.assertIsNone(parsed)


class CollectResultsTest(unittest.TestCase):
    def test_collects_cases_and_summary(self):
        lines = [
            "MSOS_CASE:uart:single-byte:PASS",
            "MSOS_CASE:uart:multi-byte:PASS",
            "MSOS_SUMMARY:uart:pass=2:total=2:result=PASS",
        ]
        run = hw_driver_runner.collect_results(lines, "uart")
        self.assertEqual(len(run.cases), 2)
        self.assertIsNotNone(run.summary)
        self.assertEqual(run.summary.pass_count, 2)

    def test_collect_ignores_other_drivers(self):
        lines = [
            "MSOS_CASE:spi:single-byte:PASS",
            "MSOS_SUMMARY:spi:pass=1:total=1:result=PASS",
            "MSOS_CASE:uart:single-byte:PASS",
            "MSOS_SUMMARY:uart:pass=1:total=1:result=PASS",
        ]
        run = hw_driver_runner.collect_results(lines, "uart")
        self.assertEqual(len(run.cases), 1)
        self.assertEqual(run.cases[0].driver, "uart")
        self.assertEqual(run.summary.driver, "uart")


class ValidateResultsTest(unittest.TestCase):
    def test_validate_passes_on_consistent_results(self):
        lines = [
            "MSOS_CASE:i2c:single-byte:PASS",
            "MSOS_CASE:i2c:wrong-address-nack:PASS",
            "MSOS_SUMMARY:i2c:pass=2:total=2:result=PASS",
        ]
        run = hw_driver_runner.collect_results(lines, "i2c")
        errors = hw_driver_runner.validate_results(run)
        self.assertEqual(errors, [])

    def test_validate_fails_on_missing_summary(self):
        lines = ["MSOS_CASE:uart:single-byte:PASS"]
        run = hw_driver_runner.collect_results(lines, "uart")
        errors = hw_driver_runner.validate_results(run)
        self.assertTrue(any("missing summary" in err for err in errors))

    def test_validate_fails_on_case_failure(self):
        lines = [
            "MSOS_CASE:spi:single-byte:FAIL",
            "MSOS_SUMMARY:spi:pass=0:total=1:result=FAIL",
        ]
        run = hw_driver_runner.collect_results(lines, "spi")
        errors = hw_driver_runner.validate_results(run)
        self.assertTrue(any("failed cases" in err for err in errors))

    def test_validate_fails_on_summary_count_mismatch(self):
        lines = [
            "MSOS_CASE:dma:memory-copy:PASS",
            "MSOS_SUMMARY:dma:pass=2:total=2:result=PASS",
        ]
        run = hw_driver_runner.collect_results(lines, "dma")
        errors = hw_driver_runner.validate_results(run)
        self.assertTrue(any("summary pass_count" in err for err in errors))


if __name__ == "__main__":
    unittest.main()
