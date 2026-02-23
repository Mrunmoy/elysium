#!/usr/bin/env python3
"""Convert a DTB binary to a C++ source file with a const byte array.

Usage:
    python3 tools/dtb2cpp.py boards/stm32f407zgt6/board.dtb -o boards/stm32f407zgt6/BoardDtb.cpp
"""

import argparse
import os
import struct
import sys

FDT_MAGIC = 0xD00DFEED


def dtb_to_cpp(dtb_bytes: bytes, source_path: str = "") -> str:
    """Convert DTB binary to C++ source with const byte array."""
    if len(dtb_bytes) < 4:
        raise ValueError("DTB too small (less than 4 bytes)")

    magic = struct.unpack(">I", dtb_bytes[:4])[0]
    if magic != FDT_MAGIC:
        raise ValueError(f"Bad DTB magic: 0x{magic:08X} (expected 0xD00DFEED)")

    lines = []
    if source_path:
        lines.append(f"// Auto-generated from {source_path} -- DO NOT EDIT")
    else:
        lines.append("// Auto-generated DTB blob -- DO NOT EDIT")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append('extern "C" const std::uint8_t g_boardDtb[] = {')

    # Format as 12 hex bytes per line
    for i in range(0, len(dtb_bytes), 12):
        chunk = dtb_bytes[i : i + 12]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        if i + 12 < len(dtb_bytes):
            hex_vals += ","
        lines.append(f"    {hex_vals}")

    lines.append("};")
    lines.append("")
    lines.append('extern "C" const std::uint32_t g_boardDtbSize = sizeof(g_boardDtb);')
    lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Convert DTB to C++ source")
    parser.add_argument("dtb", help="Input DTB file")
    parser.add_argument("-o", "--output", help="Output C++ file (default: stdout)")
    args = parser.parse_args()

    with open(args.dtb, "rb") as f:
        dtb_bytes = f.read()

    source_path = os.path.basename(args.dtb)
    cpp = dtb_to_cpp(dtb_bytes, source_path)

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w") as f:
            f.write(cpp)
    else:
        sys.stdout.write(cpp)


if __name__ == "__main__":
    main()
