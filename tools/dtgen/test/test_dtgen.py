"""Tests for the dtgen board description parser and C++ emitter."""

import pytest

from tools.dtgen.schema import parse_board_yaml, BoardDescription, ValidationError
from tools.dtgen.emitter import emit_board_config_h


# ---- Schema / Parser Tests ----


class TestParseBasic:
    """Test YAML parsing and BoardDescription structure."""

    def test_parses_board_name(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.board_name == "STM32F407ZGT6"

    def test_parses_mcu(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.mcu == "STM32F407ZGT6"

    def test_parses_arch(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.arch == "cortex-m4"

    def test_parses_system_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.clocks["system"] == 168000000

    def test_parses_apb1_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.clocks["apb1"] == 42000000

    def test_parses_apb2_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.clocks["apb2"] == 84000000

    def test_parses_hse_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.clocks.get("hse") == 8000000


class TestParseMemory:
    """Test memory region parsing."""

    def test_flash_region(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert "flash" in bd.memory
        assert bd.memory["flash"]["base"] == 0x08000000
        assert bd.memory["flash"]["size"] == 0x100000

    def test_sram_region(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert "sram" in bd.memory
        assert bd.memory["sram"]["base"] == 0x20000000
        assert bd.memory["sram"]["size"] == 0x20000

    def test_ccm_region(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert "ccm" in bd.memory
        assert bd.memory["ccm"]["base"] == 0x10000000
        assert bd.memory["ccm"]["size"] == 0x10000

    def test_ddr_region(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        assert "ddr" in bd.memory
        assert bd.memory["ddr"]["base"] == 0x00100000
        assert bd.memory["ddr"]["size"] == 0x1FF00000

    def test_no_ccm_when_absent(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        assert "ccm" not in bd.memory


class TestParseConsole:
    """Test console UART configuration parsing."""

    def test_uart_id(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.console_uart == "usart1"

    def test_baud_rate(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.console_baud == 115200

    def test_tx_pin(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.console_tx is not None
        assert bd.console_tx["port"] == "A"
        assert bd.console_tx["pin"] == 9
        assert bd.console_tx["af"] == 7

    def test_no_tx_pin_when_absent(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        assert bd.console_tx is None

    def test_rx_pin(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.console_rx is not None
        assert bd.console_rx["port"] == "A"
        assert bd.console_rx["pin"] == 10
        assert bd.console_rx["af"] == 7

    def test_no_rx_pin_when_absent(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        assert bd.console_rx is None

    def test_pynq_uart0(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        assert bd.console_uart == "uart0"

    def test_custom_baud(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        assert bd.console_baud == 9600


class TestParseLed:
    """Test LED configuration parsing."""

    def test_led_present(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.led is not None
        assert bd.led["port"] == "C"
        assert bd.led["pin"] == 13

    def test_led_absent(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        assert bd.led is None

    def test_led_absent_minimal(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        assert bd.led is None


class TestParseFeatures:
    """Test feature flag parsing."""

    def test_fpu_true(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        assert bd.features["fpu"] is True

    def test_fpu_false(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        assert bd.features["fpu"] is False


class TestValidation:
    """Test YAML validation and error handling."""

    def test_missing_board_section(self):
        yaml_str = """\
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
        with pytest.raises(ValidationError, match="board"):
            parse_board_yaml(yaml_str)

    def test_missing_clocks_section(self):
        yaml_str = """\
board:
  name: Test
  mcu: Test
  arch: cortex-m0
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
        with pytest.raises(ValidationError, match="clocks"):
            parse_board_yaml(yaml_str)

    def test_missing_console_section(self):
        yaml_str = """\
board:
  name: Test
  mcu: Test
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
features:
  fpu: false
"""
        with pytest.raises(ValidationError, match="console"):
            parse_board_yaml(yaml_str)

    def test_missing_memory_section(self):
        yaml_str = """\
board:
  name: Test
  mcu: Test
  arch: cortex-m0
clocks:
  system: 48000000
  apb1: 48000000
  apb2: 48000000
console:
  uart: usart1
  baud: 9600
features:
  fpu: false
"""
        with pytest.raises(ValidationError, match="memory"):
            parse_board_yaml(yaml_str)

    def test_missing_board_name(self):
        yaml_str = """\
board:
  mcu: Test
  arch: cortex-m0
clocks:
  system: 48000000
  apb1: 48000000
  apb2: 48000000
memory:
  flash:
    base: 0x08000000
    size: 0x10000
console:
  uart: usart1
  baud: 9600
features:
  fpu: false
"""
        with pytest.raises(ValidationError, match="name"):
            parse_board_yaml(yaml_str)

    def test_empty_yaml(self):
        with pytest.raises(ValidationError):
            parse_board_yaml("")

    def test_invalid_yaml(self):
        with pytest.raises(ValidationError):
            parse_board_yaml("{{{{not yaml")


# ---- Emitter Tests ----


class TestEmitterHeader:
    """Test generated C++ header structure."""

    def test_pragma_once(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "#pragma once" in code

    def test_auto_generated_comment(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "Auto-generated" in code
        assert "DO NOT EDIT" in code

    def test_includes_cstdint(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "#include <cstdint>" in code

    def test_board_namespace(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "namespace board" in code


class TestEmitterBoardInfo:
    """Test board identity constants in generated code."""

    def test_board_name(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert 'kBoardName' in code
        assert '"STM32F407ZGT6"' in code

    def test_mcu(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert 'kMcu' in code
        assert '"STM32F407ZGT6"' in code

    def test_arch(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert 'kArch' in code
        assert '"cortex-m4"' in code


class TestEmitterClocks:
    """Test clock constants in generated code."""

    def test_system_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kSystemClock" in code
        assert "168000000" in code

    def test_apb1_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kApb1Clock" in code
        assert "42000000" in code

    def test_apb2_clock(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kApb2Clock" in code
        assert "84000000" in code

    def test_hse_clock_present(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kHseClock" in code
        assert "8000000" in code

    def test_hse_clock_absent(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kHseClock" not in code


class TestEmitterMemory:
    """Test memory region constants in generated code."""

    def test_flash_base(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kFlashBase" in code
        assert "0x08000000" in code

    def test_flash_size(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kFlashSize" in code
        assert "0x00100000" in code

    def test_sram_base(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kSramBase" in code
        assert "0x20000000" in code

    def test_ccm_present(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kCcmBase" in code
        assert "kCcmSize" in code

    def test_ddr_region(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kDdrBase" in code
        assert "kDdrSize" in code

    def test_no_flash_on_pynq(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kFlashBase" not in code


class TestEmitterConsole:
    """Test console UART constants in generated code."""

    def test_console_uart_enum(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleUart" in code
        assert "hal::UartId::Usart1" in code

    def test_console_baud(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleBaud" in code
        assert "115200" in code

    def test_console_tx_port(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleTxPort" in code
        assert "hal::Port::A" in code

    def test_console_tx_pin(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleTxPin" in code
        assert "= 9" in code

    def test_console_tx_af(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleTxAf" in code
        assert "= 7" in code

    def test_has_console_tx_flag(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kHasConsoleTx" in code
        assert "true" in code

    def test_no_console_tx_on_pynq(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kHasConsoleTx" in code
        assert "kConsoleTxPort" not in code

    def test_console_rx_port(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleRxPort" in code
        assert "hal::Port::A" in code

    def test_console_rx_pin(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleRxPin" in code
        assert "= 10" in code

    def test_console_rx_af(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kConsoleRxAf" in code

    def test_has_console_rx_flag(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kHasConsoleRx = true" in code

    def test_no_console_rx_on_pynq(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kHasConsoleRx = false" in code
        assert "kConsoleRxPort" not in code

    def test_pynq_uart0(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "hal::UartId::Uart0" in code


class TestEmitterLed:
    """Test LED constants in generated code."""

    def test_led_present(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kHasLed" in code
        assert "kLedPort" in code
        assert "kLedPin" in code
        assert "hal::Port::C" in code
        assert "= 13" in code

    def test_led_absent(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert "kHasLed" in code
        assert "false" in code
        assert "kLedPort" not in code


class TestEmitterFeatures:
    """Test feature flags in generated code."""

    def test_fpu_true(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert "kHasFpu" in code
        assert "true" in code

    def test_fpu_false(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        code = emit_board_config_h(bd)
        assert "kHasFpu" in code
        assert "false" in code


class TestEmitterHalIncludes:
    """Test that the right HAL headers are included when needed."""

    def test_includes_uart_h(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert '#include "hal/Uart.h"' in code

    def test_includes_gpio_h_when_led(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        assert '#include "hal/Gpio.h"' in code

    def test_no_gpio_h_without_led_or_tx(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert '#include "hal/Gpio.h"' not in code


class TestEndToEnd:
    """End-to-end tests with real board YAML files."""

    def test_stm32f407_compiles(self, stm32f407_yaml):
        """Generated code should be syntactically valid C++ (basic check)."""
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd)
        # Verify balanced braces
        assert code.count("{") == code.count("}")
        # Verify no double semicolons
        assert ";;" not in code

    def test_pynq_compiles(self, pynq_yaml):
        bd = parse_board_yaml(pynq_yaml)
        code = emit_board_config_h(bd)
        assert code.count("{") == code.count("}")
        assert ";;" not in code

    def test_minimal_compiles(self, minimal_yaml):
        bd = parse_board_yaml(minimal_yaml)
        code = emit_board_config_h(bd)
        assert code.count("{") == code.count("}")
        assert ";;" not in code

    def test_source_yaml_path_in_comment(self, stm32f407_yaml):
        bd = parse_board_yaml(stm32f407_yaml)
        code = emit_board_config_h(bd, source_path="boards/stm32f407zgt6.yaml")
        assert "stm32f407zgt6.yaml" in code
