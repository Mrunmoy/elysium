"""Shared fixtures for dtgen tests."""

import pytest
import sys
import os

# Add the project root to sys.path so 'tools.dtgen' is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))


STM32F407_YAML = """\
board:
  name: STM32F407ZGT6
  mcu: STM32F407ZGT6
  arch: cortex-m4

clocks:
  system: 168000000
  apb1: 42000000
  apb2: 84000000
  hse: 8000000

memory:
  flash:
    base: 0x08000000
    size: 0x100000
  sram:
    base: 0x20000000
    size: 0x20000
  ccm:
    base: 0x10000000
    size: 0x10000

console:
  uart: usart1
  baud: 115200
  tx:
    port: A
    pin: 9
    af: 7

led:
  port: C
  pin: 13

features:
  fpu: true
"""


PYNQ_YAML = """\
board:
  name: PYNQ-Z2
  mcu: Zynq-7020
  arch: cortex-a9

clocks:
  system: 650000000
  apb1: 100000000
  apb2: 100000000

memory:
  ddr:
    base: 0x00100000
    size: 0x1FF00000

console:
  uart: uart0
  baud: 115200

features:
  fpu: true
"""


MINIMAL_YAML = """\
board:
  name: TestBoard
  mcu: TestMCU
  arch: cortex-m0

clocks:
  system: 48000000
  apb1: 48000000
  apb2: 48000000

memory:
  flash:
    base: 0x08000000
    size: 0x10000
  sram:
    base: 0x20000000
    size: 0x4000

console:
  uart: usart1
  baud: 9600

features:
  fpu: false
"""


@pytest.fixture
def stm32f407_yaml():
    """STM32F407 board YAML string."""
    return STM32F407_YAML


@pytest.fixture
def pynq_yaml():
    """PYNQ-Z2 board YAML string."""
    return PYNQ_YAML


@pytest.fixture
def minimal_yaml():
    """Minimal board YAML with no optional fields."""
    return MINIMAL_YAML
