#!/usr/bin/env python3
"""Hardware-backed STM32F407 driver validation runner."""

from __future__ import annotations

import argparse
import dataclasses
import datetime
import os
import pathlib
import re
import select
import subprocess
import sys
import termios
import time
import tty
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Union


CASE_RE = re.compile(r"^MSOS_CASE:([a-z0-9_-]+):([a-z0-9_-]+):(PASS|FAIL)$")
SUMMARY_RE = re.compile(
    r"^MSOS_SUMMARY:([a-z0-9_-]+):pass=(\d+):total=(\d+):result=(PASS|FAIL)$"
)


@dataclasses.dataclass
class CaseResult:
    driver: str
    case_name: str
    passed: bool


@dataclasses.dataclass
class SummaryResult:
    driver: str
    pass_count: int
    total_count: int
    passed: bool


@dataclasses.dataclass
class DriverRunResult:
    driver: str
    cases: List[CaseResult]
    summary: Optional[SummaryResult]
    raw_lines: List[str]


@dataclasses.dataclass(frozen=True)
class DriverScenario:
    driver: str
    board1_app: str
    board2_app: Optional[str]


SCENARIOS: Dict[str, DriverScenario] = {
    "uart": DriverScenario("uart", "uart2-test", "uart2-echo"),
    "spi": DriverScenario("spi", "spi2-test", "spi2-slave"),
    "i2c": DriverScenario("i2c", "i2c-test", "i2c-slave"),
    "dma": DriverScenario("dma", "dma-test", None),
}


def parse_machine_line(line: str) -> Optional[Union[CaseResult, SummaryResult]]:
    text = line.strip()

    case_match = CASE_RE.match(text)
    if case_match:
        return CaseResult(
            driver=case_match.group(1),
            case_name=case_match.group(2),
            passed=case_match.group(3) == "PASS",
        )

    summary_match = SUMMARY_RE.match(text)
    if summary_match:
        return SummaryResult(
            driver=summary_match.group(1),
            pass_count=int(summary_match.group(2)),
            total_count=int(summary_match.group(3)),
            passed=summary_match.group(4) == "PASS",
        )

    return None


def collect_results(lines: Iterable[str], driver: str) -> DriverRunResult:
    cases: List[CaseResult] = []
    summary: Optional[SummaryResult] = None
    raw_lines = [line.rstrip("\r\n") for line in lines]

    for line in raw_lines:
        parsed = parse_machine_line(line)
        if parsed is None:
            continue
        if isinstance(parsed, CaseResult):
            if parsed.driver == driver:
                cases.append(parsed)
            continue
        if parsed.driver == driver:
            summary = parsed

    return DriverRunResult(driver=driver, cases=cases, summary=summary, raw_lines=raw_lines)


def validate_results(result: DriverRunResult) -> List[str]:
    errors: List[str] = []

    if result.summary is None:
        errors.append(f"{result.driver}: missing summary line")
        return errors

    failed_cases = [case.case_name for case in result.cases if not case.passed]
    passed_cases = len(result.cases) - len(failed_cases)

    if failed_cases:
        errors.append(f"{result.driver}: failed cases: {', '.join(failed_cases)}")

    if result.summary.pass_count != passed_cases:
        errors.append(
            f"{result.driver}: summary pass_count={result.summary.pass_count} "
            f"does not match passed_cases={passed_cases}"
        )

    if result.summary.total_count != len(result.cases):
        errors.append(
            f"{result.driver}: summary total_count={result.summary.total_count} "
            f"does not match observed_cases={len(result.cases)}"
        )

    if not result.summary.passed:
        errors.append(f"{result.driver}: summary reports FAIL")

    return errors


def run_command(cmd: Sequence[str], cwd: pathlib.Path) -> None:
    print(f">>> {' '.join(cmd)}")
    subprocess.run(cmd, cwd=str(cwd), check=True)


def flash_app(
    project_root: pathlib.Path,
    target: str,
    app: str,
    probe: str,
) -> None:
    run_command(
        [
            sys.executable,
            "build.py",
            "-f",
            "--target",
            target,
            "--app",
            app,
            "--probe",
            probe,
        ],
        cwd=project_root,
    )


def baud_to_termios(baud: int) -> int:
    mapping = {
        9600: termios.B9600,
        19200: termios.B19200,
        38400: termios.B38400,
        57600: termios.B57600,
        115200: termios.B115200,
        230400: termios.B230400,
    }
    if baud not in mapping:
        raise ValueError(f"Unsupported baud rate: {baud}")
    return mapping[baud]


def configure_serial(fd: int, baud: int) -> None:
    speed = baud_to_termios(baud)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = speed
    attrs[5] = speed
    attrs[2] |= termios.CLOCAL | termios.CREAD
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def capture_serial_lines(
    port: str,
    baud: int,
    timeout_sec: float,
    stop_driver: str,
    trigger: Optional[Callable[[], None]] = None,
) -> List[str]:
    lines: List[str] = []
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    try:
        configure_serial(fd, baud)

        # Drain stale bytes before we trigger a reboot/flash cycle.
        while True:
            readable, _, _ = select.select([fd], [], [], 0.05)
            if not readable:
                break
            _ = os.read(fd, 4096)

        if trigger is not None:
            trigger()

        deadline = time.monotonic() + timeout_sec
        buffer = b""

        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.2)
            if not readable:
                continue

            chunk = os.read(fd, 4096)
            if not chunk:
                continue

            buffer += chunk
            while b"\n" in buffer:
                raw_line, buffer = buffer.split(b"\n", 1)
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                lines.append(line)
                print(f"[{stop_driver}] {line}")
                if line.startswith(f"MSOS_SUMMARY:{stop_driver}:"):
                    return lines

        return lines
    finally:
        os.close(fd)


def resolve_drivers(raw_drivers: str) -> List[str]:
    drivers = [item.strip().lower() for item in raw_drivers.split(",") if item.strip()]
    if not drivers:
        raise ValueError("At least one driver is required")

    invalid = [driver for driver in drivers if driver not in SCENARIOS]
    if invalid:
        raise ValueError(f"Unknown drivers: {', '.join(invalid)}")

    return drivers


def create_artifact_dir(base_dir: pathlib.Path) -> pathlib.Path:
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = base_dir / ts
    out.mkdir(parents=True, exist_ok=True)
    return out


def run_scenario(
    project_root: pathlib.Path,
    scenario: DriverScenario,
    target: str,
    board1_probe: str,
    board2_probe: str,
    board1_port: str,
    baud: int,
    timeout_sec: float,
    artifact_dir: Optional[pathlib.Path],
) -> DriverRunResult:
    if scenario.board2_app is not None:
        flash_app(project_root, target, scenario.board2_app, board2_probe)

    lines = capture_serial_lines(
        port=board1_port,
        baud=baud,
        timeout_sec=timeout_sec,
        stop_driver=scenario.driver,
        trigger=lambda: flash_app(project_root, target, scenario.board1_app, board1_probe),
    )

    if artifact_dir is not None:
        out_file = artifact_dir / f"{scenario.driver}.log"
        out_file.write_text("\n".join(lines) + "\n", encoding="utf-8")

    return collect_results(lines, scenario.driver)


def parse_args() -> argparse.Namespace:
    project_root = pathlib.Path(__file__).resolve().parent.parent

    parser = argparse.ArgumentParser(description="Run on-target driver validation scenarios")
    parser.add_argument("--target", default="stm32f407zgt6")
    parser.add_argument("--drivers", default="uart,spi,i2c,dma")
    parser.add_argument("--board1-probe", default="jlink")
    parser.add_argument("--board2-probe", default="stlink")
    parser.add_argument("--board1-port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--project-root", default=str(project_root))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--register-trace", action="store_true")
    parser.add_argument("--artifact-dir", default=str(project_root / "artifacts" / "hw-driver-run"))

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = pathlib.Path(args.project_root).resolve()

    try:
        selected_drivers = resolve_drivers(args.drivers)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    artifact_dir: Optional[pathlib.Path] = None
    if args.register_trace:
        artifact_dir = create_artifact_dir(pathlib.Path(args.artifact_dir))
        print(f"Register-trace artifacts: {artifact_dir}")

    if not args.skip_build:
        run_command(
            [sys.executable, "build.py", "--target", args.target],
            cwd=project_root,
        )

    all_errors: List[str] = []

    for driver in selected_drivers:
        scenario = SCENARIOS[driver]
        print(f"\n=== Scenario: {driver} ===")
        result = run_scenario(
            project_root=project_root,
            scenario=scenario,
            target=args.target,
            board1_probe=args.board1_probe,
            board2_probe=args.board2_probe,
            board1_port=args.board1_port,
            baud=args.baud,
            timeout_sec=args.timeout,
            artifact_dir=artifact_dir,
        )

        errors = validate_results(result)
        if errors:
            all_errors.extend(errors)
            print(f"[FAIL] {driver}")
            for err in errors:
                print(f"  - {err}")
        else:
            print(f"[PASS] {driver}")

    if all_errors:
        if artifact_dir is not None:
            print(f"Logs: {artifact_dir}")
        return 1

    print("\nAll selected scenarios passed.")
    if artifact_dir is not None:
        print(f"Logs: {artifact_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
