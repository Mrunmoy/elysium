#!/usr/bin/env python3
"""
ms-os Crash Monitor -- serial monitor with automatic crash dump decoding.

Monitors a serial port for ms-os crash dump output. When a crash dump is
detected (delimited by === CRASH DUMP BEGIN === / === CRASH DUMP END ===),
extracts PC and LR addresses and runs arm-none-eabi-addr2line to translate
them to source file:line.

Inspired by ESP-IDF idf_monitor and the STM32F407 crash_monitor.py.

Usage:
    python3 tools/crash_monitor.py --port /dev/ttyUSB0 --elf build/app/threads/threads
    python3 tools/crash_monitor.py --port /dev/ttyUSB0 --baud 115200 --elf build/app/threads/threads
"""

import argparse
import re
import subprocess
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip3 install pyserial")
    sys.exit(1)


# ANSI color codes
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
RESET = "\033[0m"
BOLD = "\033[1m"

# Crash dump markers
CRASH_BEGIN = "=== CRASH DUMP BEGIN ==="
CRASH_END = "=== CRASH DUMP END ==="

# Regex to match register values in the crash dump
# Matches lines like "  PC  : 08000ABC" or "  LR  : 08000A34"
RE_REGISTER = re.compile(r"^\s+(PC|LR)\s*:\s*([0-9A-Fa-f]{8})\s*$")

# Regex to match any 8-char hex value that looks like a FLASH address (0x0800xxxx)
RE_FLASH_ADDR = re.compile(r"\b(0800[0-9A-Fa-f]{4})\b")


def timestamp():
    """Return current time as HH:MM:SS.mmm string."""
    now = datetime.now()
    return now.strftime("%H:%M:%S.") + f"{now.microsecond // 1000:03d}"


def decode_addresses(elf_path, addresses, toolchain_prefix="arm-none-eabi-"):
    """
    Run addr2line to decode a list of hex addresses to source file:line.

    Returns a dict mapping address -> "function() at file:line" string.
    """
    if not addresses or not elf_path:
        return {}

    addr2line = f"{toolchain_prefix}addr2line"
    cmd = [addr2line, "-fiaC", "-e", elf_path] + [f"0x{a}" for a in addresses]

    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True)
    except FileNotFoundError:
        print(f"{YELLOW}Warning: {addr2line} not found. "
              f"Install ARM GCC toolchain for address decoding.{RESET}")
        return {}
    except subprocess.CalledProcessError as e:
        print(f"{YELLOW}Warning: addr2line failed: {e.output}{RESET}")
        return {}

    # Parse addr2line output. Format:
    #   0x08000ABC
    #   function_name
    #   /path/to/file.cpp:42
    result = {}
    lines = output.strip().split("\n")
    i = 0
    addr_idx = 0
    while i < len(lines) and addr_idx < len(addresses):
        line = lines[i].strip()

        # Skip address echo line (starts with 0x)
        if line.startswith("0x"):
            i += 1
            continue

        # Function name line
        func = line
        i += 1

        # File:line line
        if i < len(lines):
            location = lines[i].strip()
            i += 1
        else:
            location = "??:?"

        if addr_idx < len(addresses):
            addr = addresses[addr_idx]
            if location.startswith("??"):
                result[addr] = f"{func} at <unknown>"
            else:
                result[addr] = f"{func} at {location}"
            addr_idx += 1

    return result


def process_crash_dump(crash_lines, elf_path):
    """
    Process collected crash dump lines:
    1. Extract PC and LR addresses
    2. Run addr2line to decode them
    3. Print decoded crash location
    """
    addresses = {}  # label -> hex address

    for line in crash_lines:
        match = RE_REGISTER.match(line)
        if match:
            label = match.group(1)
            addr = match.group(2)
            addresses[label] = addr

    if not addresses:
        print(f"{YELLOW}No addresses found in crash dump.{RESET}")
        return

    # Decode all addresses
    addr_list = list(addresses.values())
    decoded = decode_addresses(elf_path, addr_list)

    # Print decoded crash location
    print()
    print(f"{GREEN}{BOLD}=== Decoded Crash Location ==={RESET}")
    for label, addr in addresses.items():
        location = decoded.get(addr, "<decode failed>")
        print(f"{GREEN}  {label}: 0x{addr} -> {location}{RESET}")
    print(f"{GREEN}{BOLD}=============================={RESET}")
    print()


def monitor(port, baud, elf_path):
    """Main serial monitor loop."""
    print(f"{CYAN}ms-os Crash Monitor{RESET}")
    print(f"  Port: {port}")
    print(f"  Baud: {baud}")
    if elf_path:
        print(f"  ELF:  {elf_path}")
    else:
        print(f"  ELF:  {YELLOW}(none -- address decoding disabled){RESET}")
    print(f"  Press Ctrl+C to exit")
    print()

    in_crash = False
    crash_lines = []
    line_buffer = ""

    while True:
        try:
            ser = serial.Serial(port, baud, timeout=0.1)
            print(f"{CYAN}[{timestamp()}] Connected to {port}{RESET}")
        except serial.SerialException as e:
            print(f"{YELLOW}[{timestamp()}] Cannot open {port}: {e}{RESET}")
            print(f"{YELLOW}  Retrying in 2 seconds...{RESET}")
            time.sleep(2)
            continue

        try:
            while True:
                data = ser.read(ser.in_waiting or 1)
                if not data:
                    continue

                # Decode bytes to string, handling partial UTF-8
                try:
                    text = data.decode("utf-8", errors="replace")
                except Exception:
                    text = data.decode("ascii", errors="replace")

                # Process character by character for line buffering
                for ch in text:
                    if ch == "\n":
                        line = line_buffer.rstrip("\r")
                        line_buffer = ""

                        # Check for crash dump markers
                        if CRASH_BEGIN in line:
                            in_crash = True
                            crash_lines = []
                            print(f"{RED}{BOLD}[{timestamp()}] {line}{RESET}")
                            continue

                        if CRASH_END in line:
                            print(f"{RED}{BOLD}[{timestamp()}] {line}{RESET}")
                            in_crash = False
                            process_crash_dump(crash_lines, elf_path)
                            crash_lines = []
                            continue

                        if in_crash:
                            crash_lines.append(line)
                            print(f"{RED}[{timestamp()}] {line}{RESET}")
                        else:
                            print(f"[{timestamp()}] {line}")
                    else:
                        line_buffer += ch

        except serial.SerialException:
            print(f"{YELLOW}[{timestamp()}] Serial disconnected. Reconnecting...{RESET}")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1)

        except KeyboardInterrupt:
            print(f"\n{CYAN}Exiting.{RESET}")
            try:
                ser.close()
            except Exception:
                pass
            return


def main():
    parser = argparse.ArgumentParser(
        description="ms-os Crash Monitor -- serial monitor with crash dump decoding"
    )
    parser.add_argument(
        "--port", "-p",
        default="/dev/ttyUSB0",
        help="Serial port (default: /dev/ttyUSB0)"
    )
    parser.add_argument(
        "--baud", "-b",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--elf", "-e",
        default=None,
        help="Path to ELF file for address decoding (e.g. build/app/threads/threads)"
    )
    parser.add_argument(
        "--toolchain-prefix",
        default="arm-none-eabi-",
        help="Toolchain prefix for addr2line (default: arm-none-eabi-)"
    )
    args = parser.parse_args()

    monitor(args.port, args.baud, args.elf)


if __name__ == "__main__":
    main()
