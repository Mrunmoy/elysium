#!/usr/bin/env python3
"""Build DTB binaries for all ms-os boards.

Generates standard FDT binaries (equivalent to dtc -I dts -O dtb).
Each board's DTB is written to boards/<board>/board.dtb.

Usage:
    python3 tools/build_dtbs.py
"""

import os
import sys

# Add tools/ to path for fdtlib import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fdtlib import FdtNode, FdtProperty, build_dtb


def build_stm32f207zgt6() -> bytes:
    root = FdtNode("")
    root.add_property(FdtProperty.from_string("compatible", "ms-os,stm32f207zgt6"))
    root.add_property(FdtProperty.from_string("model", "STM32F207ZGT6"))

    board = FdtNode("board")
    board.add_property(FdtProperty.from_string("name", "STM32F207ZGT6"))
    board.add_property(FdtProperty.from_string("mcu", "STM32F207ZGT6"))
    board.add_property(FdtProperty.from_string("arch", "cortex-m3"))
    root.add_child(board)

    clocks = FdtNode("clocks")
    clocks.add_property(FdtProperty.from_u32("system-clock", 120000000))
    clocks.add_property(FdtProperty.from_u32("apb1-clock", 30000000))
    clocks.add_property(FdtProperty.from_u32("apb2-clock", 60000000))
    clocks.add_property(FdtProperty.from_u32("hse-clock", 25000000))
    root.add_child(clocks)

    memory = FdtNode("memory")
    flash = FdtNode("flash")
    flash.add_property(FdtProperty.from_u32_pair("reg", 0x08000000, 0x100000))
    memory.add_child(flash)
    sram = FdtNode("sram")
    sram.add_property(FdtProperty.from_u32_pair("reg", 0x20000000, 0x20000))
    memory.add_child(sram)
    root.add_child(memory)

    console = FdtNode("console")
    console.add_property(FdtProperty.from_string("uart", "usart1"))
    console.add_property(FdtProperty.from_u32("baud", 115200))
    tx = FdtNode("tx")
    tx.add_property(FdtProperty.from_string("port", "A"))
    tx.add_property(FdtProperty.from_u32("pin", 9))
    tx.add_property(FdtProperty.from_u32("af", 7))
    console.add_child(tx)
    rx = FdtNode("rx")
    rx.add_property(FdtProperty.from_string("port", "A"))
    rx.add_property(FdtProperty.from_u32("pin", 10))
    rx.add_property(FdtProperty.from_u32("af", 7))
    console.add_child(rx)
    root.add_child(console)

    led = FdtNode("led")
    led.add_property(FdtProperty.from_string("port", "C"))
    led.add_property(FdtProperty.from_u32("pin", 13))
    root.add_child(led)

    features = FdtNode("features")
    root.add_child(features)

    return build_dtb(root)


def build_stm32f407zgt6() -> bytes:
    root = FdtNode("")
    root.add_property(FdtProperty.from_string("compatible", "ms-os,stm32f407zgt6"))
    root.add_property(FdtProperty.from_string("model", "STM32F407ZGT6"))

    board = FdtNode("board")
    board.add_property(FdtProperty.from_string("name", "STM32F407ZGT6"))
    board.add_property(FdtProperty.from_string("mcu", "STM32F407ZGT6"))
    board.add_property(FdtProperty.from_string("arch", "cortex-m4"))
    root.add_child(board)

    clocks = FdtNode("clocks")
    clocks.add_property(FdtProperty.from_u32("system-clock", 168000000))
    clocks.add_property(FdtProperty.from_u32("apb1-clock", 42000000))
    clocks.add_property(FdtProperty.from_u32("apb2-clock", 84000000))
    clocks.add_property(FdtProperty.from_u32("hse-clock", 8000000))
    root.add_child(clocks)

    memory = FdtNode("memory")
    flash = FdtNode("flash")
    flash.add_property(FdtProperty.from_u32_pair("reg", 0x08000000, 0x100000))
    memory.add_child(flash)
    sram = FdtNode("sram")
    sram.add_property(FdtProperty.from_u32_pair("reg", 0x20000000, 0x20000))
    memory.add_child(sram)
    ccm = FdtNode("ccm")
    ccm.add_property(FdtProperty.from_u32_pair("reg", 0x10000000, 0x10000))
    memory.add_child(ccm)
    root.add_child(memory)

    console = FdtNode("console")
    console.add_property(FdtProperty.from_string("uart", "usart1"))
    console.add_property(FdtProperty.from_u32("baud", 115200))
    tx = FdtNode("tx")
    tx.add_property(FdtProperty.from_string("port", "A"))
    tx.add_property(FdtProperty.from_u32("pin", 9))
    tx.add_property(FdtProperty.from_u32("af", 7))
    console.add_child(tx)
    rx = FdtNode("rx")
    rx.add_property(FdtProperty.from_string("port", "A"))
    rx.add_property(FdtProperty.from_u32("pin", 10))
    rx.add_property(FdtProperty.from_u32("af", 7))
    console.add_child(rx)
    root.add_child(console)

    led = FdtNode("led")
    led.add_property(FdtProperty.from_string("port", "C"))
    led.add_property(FdtProperty.from_u32("pin", 13))
    root.add_child(led)

    features = FdtNode("features")
    features.add_property(FdtProperty.from_bool("fpu"))
    root.add_child(features)

    return build_dtb(root)


def build_pynq_z2() -> bytes:
    root = FdtNode("")
    root.add_property(FdtProperty.from_string("compatible", "ms-os,pynq-z2"))
    root.add_property(FdtProperty.from_string("model", "PYNQ-Z2"))

    board = FdtNode("board")
    board.add_property(FdtProperty.from_string("name", "PYNQ-Z2"))
    board.add_property(FdtProperty.from_string("mcu", "Zynq-7020"))
    board.add_property(FdtProperty.from_string("arch", "cortex-a9"))
    root.add_child(board)

    clocks = FdtNode("clocks")
    clocks.add_property(FdtProperty.from_u32("system-clock", 650000000))
    clocks.add_property(FdtProperty.from_u32("apb1-clock", 100000000))
    clocks.add_property(FdtProperty.from_u32("apb2-clock", 100000000))
    root.add_child(clocks)

    memory = FdtNode("memory")
    ddr = FdtNode("ddr")
    ddr.add_property(FdtProperty.from_u32_pair("reg", 0x00100000, 0x1FF00000))
    memory.add_child(ddr)
    root.add_child(memory)

    console = FdtNode("console")
    console.add_property(FdtProperty.from_string("uart", "uart0"))
    console.add_property(FdtProperty.from_u32("baud", 115200))
    root.add_child(console)

    features = FdtNode("features")
    features.add_property(FdtProperty.from_bool("fpu"))
    root.add_child(features)

    return build_dtb(root)


BOARDS = {
    "stm32f207zgt6": build_stm32f207zgt6,
    "stm32f407zgt6": build_stm32f407zgt6,
    "pynq-z2": build_pynq_z2,
}


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    boards_dir = os.path.join(project_root, "boards")

    for name, builder in BOARDS.items():
        dtb = builder()
        out_dir = os.path.join(boards_dir, name)
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, "board.dtb")
        with open(out_path, "wb") as f:
            f.write(dtb)
        print(f"  {name}: {len(dtb)} bytes -> {out_path}")

    print(f"Built {len(BOARDS)} DTBs")


if __name__ == "__main__":
    main()
