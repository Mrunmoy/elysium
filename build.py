#!/usr/bin/env python3
"""
ms-os build script.

Usage:
    python3 build.py              # Cross-compile firmware
    python3 build.py -c           # Clean build
    python3 build.py -t           # Build + run host tests
    python3 build.py -e           # Build + examples (same as default for now)
    python3 build.py -f           # Flash to target via OpenOCD
    python3 build.py -c -t        # Clean + tests
"""

import argparse
import os
import shutil
import subprocess
import sys

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
TEST_BUILD_DIR = os.path.join(PROJECT_DIR, "build-test")
TOOLCHAIN_FILE = os.path.join(PROJECT_DIR, "cmake", "arm-none-eabi-gcc.cmake")


def run(cmd, cwd=None):
    """Run a command, printing it first. Exit on failure."""
    print(f">>> {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(result.returncode)


def clean():
    """Remove build directories."""
    for d in [BUILD_DIR, TEST_BUILD_DIR]:
        if os.path.exists(d):
            print(f"Removing {d}")
            shutil.rmtree(d)


def build_firmware():
    """Cross-compile firmware for ARM target."""
    os.makedirs(BUILD_DIR, exist_ok=True)

    run([
        "cmake",
        "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE}",
        "-DCMAKE_BUILD_TYPE=Debug",
        PROJECT_DIR,
    ], cwd=BUILD_DIR)

    run(["ninja"], cwd=BUILD_DIR)


def build_tests():
    """Build and run host-side unit tests."""
    os.makedirs(TEST_BUILD_DIR, exist_ok=True)

    run([
        "cmake",
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        PROJECT_DIR,
    ], cwd=TEST_BUILD_DIR)

    run(["ninja"], cwd=TEST_BUILD_DIR)

    run([
        "ctest",
        "--test-dir", TEST_BUILD_DIR,
        "--output-on-failure",
    ])


def flash():
    """Flash firmware to target via OpenOCD."""
    elf_path = os.path.join(BUILD_DIR, "app", "blinky", "blinky")
    if not os.path.exists(elf_path):
        print("Error: firmware not built. Run build first.")
        sys.exit(1)

    run([
        "openocd",
        "-f", "interface/stlink.cfg",
        "-f", "target/stm32f4x.cfg",
        "-c", f"program {elf_path} verify reset exit",
    ])


def main():
    parser = argparse.ArgumentParser(description="ms-os build script")
    parser.add_argument("-c", "--clean", action="store_true", help="Clean build directories")
    parser.add_argument("-t", "--test", action="store_true", help="Build and run host tests")
    parser.add_argument("-e", "--examples", action="store_true", help="Build examples")
    parser.add_argument("-f", "--flash", action="store_true", help="Flash to target")
    args = parser.parse_args()

    if args.clean:
        clean()

    if args.test:
        build_tests()
    elif args.flash:
        build_firmware()
        flash()
    else:
        build_firmware()


if __name__ == "__main__":
    main()
