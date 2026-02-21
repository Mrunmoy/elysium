#!/usr/bin/env python3
"""
ms-os build script.

Usage:
    python3 build.py                          # Cross-compile firmware (F207 default)
    python3 build.py --target stm32f407zgt6   # Cross-compile for F407
    python3 build.py --target pynq-z2         # Cross-compile for PYNQ-Z2
    python3 build.py -c                       # Clean build
    python3 build.py -t                       # Build + run host tests
    python3 build.py -e                       # Build + examples (same as default for now)
    python3 build.py -f                       # Flash to target via J-Link
    python3 build.py -f --target pynq-z2      # Flash to PYNQ-Z2 via OpenOCD
    python3 build.py -c -t                    # Clean + tests
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

# Map MSOS_TARGET to J-Link device name
JLINK_DEVICE_MAP = {
    "stm32f207zgt6": "STM32F207ZG",
    "stm32f407zgt6": "STM32F407ZG",
}

# Default app per target
DEFAULT_APP = {
    "pynq-z2": "hello",
}


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


def check_target_mismatch(target):
    """Auto-clean if the cached target differs from the requested one."""
    cache_file = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if not os.path.exists(cache_file):
        return

    with open(cache_file, "r") as f:
        for line in f:
            if line.startswith("MSOS_TARGET:"):
                cached = line.split("=", 1)[1].strip()
                if cached != target:
                    print(f"Target changed from {cached} to {target}, cleaning build dir")
                    shutil.rmtree(BUILD_DIR)
                return


def build_firmware(target):
    """Cross-compile firmware for ARM target."""
    check_target_mismatch(target)
    os.makedirs(BUILD_DIR, exist_ok=True)

    run([
        "cmake",
        "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE}",
        f"-DMSOS_TARGET={target}",
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


def flash_jlink(target, app):
    """Flash firmware to STM32 target via J-Link."""
    bin_path = os.path.join(BUILD_DIR, "app", app, f"{app}.bin")
    if not os.path.exists(bin_path):
        print(f"Error: {bin_path} not found. Run build first.")
        sys.exit(1)

    jlink_device = JLINK_DEVICE_MAP.get(target, "STM32F207ZG")

    jlink_script = os.path.join(BUILD_DIR, "flash.jlink")
    with open(jlink_script, "w") as f:
        f.write(f"loadbin {bin_path}, 0x08000000\n")
        f.write("r\n")
        f.write("g\n")
        f.write("q\n")

    run([
        "JLinkExe",
        "-device", jlink_device,
        "-if", "SWD",
        "-speed", "4000",
        "-autoconnect", "1",
        "-CommandFile", jlink_script,
    ])


def flash_openocd(app):
    """Load firmware to PYNQ-Z2 via OpenOCD JTAG."""
    elf_path = os.path.join(BUILD_DIR, "app", app, app)
    if not os.path.exists(elf_path):
        print(f"Error: {elf_path} not found. Run build first.")
        sys.exit(1)

    openocd_cfg = os.path.join(PROJECT_DIR, "openocd", "pynq-z2.cfg")
    if not os.path.exists(openocd_cfg):
        print(f"Error: {openocd_cfg} not found.")
        sys.exit(1)

    run([
        "openocd",
        "-f", openocd_cfg,
        "-c", "init",
        "-c", "halt",
        "-c", f"load_image {elf_path}",
        "-c", "resume 0x00100000",
        "-c", "shutdown",
    ])


def flash(target, app):
    """Flash firmware to target."""
    if target == "pynq-z2":
        flash_openocd(app)
    else:
        flash_jlink(target, app)


def main():
    parser = argparse.ArgumentParser(description="ms-os build script")
    parser.add_argument("-c", "--clean", action="store_true", help="Clean build directories")
    parser.add_argument("-t", "--test", action="store_true", help="Build and run host tests")
    parser.add_argument("-e", "--examples", action="store_true", help="Build examples")
    parser.add_argument("-f", "--flash", action="store_true", help="Flash to target")
    parser.add_argument("--app", default=None, help="App to flash (default: threads or hello)")
    parser.add_argument("--target", default="stm32f207zgt6",
                        choices=["stm32f207zgt6", "stm32f407zgt6", "pynq-z2"],
                        help="Target MCU (default: stm32f207zgt6)")
    args = parser.parse_args()

    # Default app depends on target
    if args.app is None:
        args.app = DEFAULT_APP.get(args.target, "threads")

    if args.clean:
        clean()
        if not (args.test or args.flash or args.examples):
            return

    if args.test:
        build_tests()
    elif args.flash:
        build_firmware(args.target)
        flash(args.target, args.app)
    else:
        build_firmware(args.target)


if __name__ == "__main__":
    main()
