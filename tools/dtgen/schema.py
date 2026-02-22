"""
YAML board description parser and validator for dtgen.

Parses a YAML board description file into a BoardDescription dataclass,
validating required fields and types.
"""

import yaml
from dataclasses import dataclass, field
from typing import Dict, List, Optional


class ValidationError(Exception):
    """Raised when a board YAML file fails validation."""
    pass


@dataclass
class BoardDescription:
    """Parsed board description."""
    board_name: str
    mcu: str
    arch: str
    clocks: Dict[str, int]
    memory: Dict[str, Dict[str, int]]
    console_uart: str
    console_baud: int
    console_tx: Optional[Dict] = None
    led: Optional[Dict] = None
    features: Dict[str, bool] = field(default_factory=dict)


def _require(data: dict, key: str, context: str = "root") -> object:
    """Require a key in a dict, raising ValidationError if missing."""
    if key not in data or data[key] is None:
        raise ValidationError(
            f"Missing required field '{key}' in {context} section"
        )
    return data[key]


def parse_board_yaml(yaml_str: str) -> BoardDescription:
    """Parse a YAML board description string into a BoardDescription.

    Args:
        yaml_str: YAML string containing the board description.

    Returns:
        BoardDescription with all parsed fields.

    Raises:
        ValidationError: If required fields are missing or invalid.
    """
    if not yaml_str or not yaml_str.strip():
        raise ValidationError("Empty YAML input")

    try:
        data = yaml.safe_load(yaml_str)
    except yaml.YAMLError as e:
        raise ValidationError(f"Invalid YAML: {e}")

    if not isinstance(data, dict):
        raise ValidationError("YAML root must be a mapping")

    # ---- board section ----
    board_section = _require(data, "board")
    board_name = _require(board_section, "name", "board")
    mcu = _require(board_section, "mcu", "board")
    arch = _require(board_section, "arch", "board")

    # ---- clocks section ----
    clocks_section = _require(data, "clocks")
    clocks = {}
    for key in ("system", "apb1", "apb2"):
        clocks[key] = int(_require(clocks_section, key, "clocks"))
    if "hse" in clocks_section and clocks_section["hse"] is not None:
        clocks["hse"] = int(clocks_section["hse"])

    # ---- memory section ----
    memory_section = _require(data, "memory")
    memory = {}
    for region_name, region_data in memory_section.items():
        if not isinstance(region_data, dict):
            raise ValidationError(
                f"Memory region '{region_name}' must be a mapping with base and size"
            )
        base = _require(region_data, "base", f"memory.{region_name}")
        size = _require(region_data, "size", f"memory.{region_name}")
        memory[region_name] = {"base": int(base), "size": int(size)}

    # ---- console section ----
    console_section = _require(data, "console")
    console_uart = str(_require(console_section, "uart", "console"))
    console_baud = int(_require(console_section, "baud", "console"))

    console_tx = None
    if "tx" in console_section and console_section["tx"] is not None:
        tx = console_section["tx"]
        console_tx = {
            "port": str(_require(tx, "port", "console.tx")),
            "pin": int(_require(tx, "pin", "console.tx")),
            "af": int(_require(tx, "af", "console.tx")),
        }

    # ---- led section (optional) ----
    led = None
    if "led" in data and data["led"] is not None:
        led_section = data["led"]
        led = {
            "port": str(_require(led_section, "port", "led")),
            "pin": int(_require(led_section, "pin", "led")),
        }

    # ---- features section (optional, defaults to empty) ----
    features = {}
    if "features" in data and data["features"] is not None:
        features_section = data["features"]
        if "fpu" in features_section:
            features["fpu"] = bool(features_section["fpu"])

    return BoardDescription(
        board_name=str(board_name),
        mcu=str(mcu),
        arch=str(arch),
        clocks=clocks,
        memory=memory,
        console_uart=console_uart,
        console_baud=console_baud,
        console_tx=console_tx,
        led=led,
        features=features,
    )
