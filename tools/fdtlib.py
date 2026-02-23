"""Minimal FDT (Flattened Device Tree) builder.

Constructs standard DTB binaries from Python data structures.
Produces the same output as 'dtc -I dts -O dtb'.

DTB format (big-endian):
  - 40-byte header
  - Memory reservation block (empty, 16 bytes of zeros)
  - Structure block (FDT tokens + property data)
  - Strings block (null-terminated property names)
"""

import struct
from typing import Dict, List, Optional, Tuple, Union


# FDT tokens
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9

# FDT magic number
FDT_MAGIC = 0xD00DFEED

# FDT version
FDT_VERSION = 17
FDT_LAST_COMP_VERSION = 16


class FdtProperty:
    """A device tree property with name and value."""

    def __init__(self, name: str, value: bytes):
        self.name = name
        self.value = value

    @staticmethod
    def from_string(name: str, text: str) -> "FdtProperty":
        """Create a string property (null-terminated)."""
        return FdtProperty(name, text.encode("ascii") + b"\x00")

    @staticmethod
    def from_u32(name: str, value: int) -> "FdtProperty":
        """Create a single uint32 property (big-endian)."""
        return FdtProperty(name, struct.pack(">I", value))

    @staticmethod
    def from_u32_pair(name: str, val1: int, val2: int) -> "FdtProperty":
        """Create a two-element uint32 property (big-endian)."""
        return FdtProperty(name, struct.pack(">II", val1, val2))

    @staticmethod
    def from_bool(name: str) -> "FdtProperty":
        """Create a boolean (empty) property."""
        return FdtProperty(name, b"")


class FdtNode:
    """A device tree node with properties and children."""

    def __init__(self, name: str):
        self.name = name
        self.properties: List[FdtProperty] = []
        self.children: List["FdtNode"] = []

    def add_property(self, prop: FdtProperty) -> "FdtNode":
        self.properties.append(prop)
        return self

    def add_child(self, child: "FdtNode") -> "FdtNode":
        self.children.append(child)
        return self


def _align4(size: int) -> int:
    """Round up to 4-byte alignment."""
    return (size + 3) & ~3


def build_dtb(root: FdtNode) -> bytes:
    """Build a DTB binary from a root node."""
    # Collect all property names into strings block
    strings_map: Dict[str, int] = {}
    strings_data = bytearray()

    def collect_strings(node: FdtNode):
        for prop in node.properties:
            if prop.name not in strings_map:
                strings_map[prop.name] = len(strings_data)
                strings_data.extend(prop.name.encode("ascii") + b"\x00")
        for child in node.children:
            collect_strings(child)

    collect_strings(root)

    # Build structure block
    struct_data = bytearray()

    def emit_node(node: FdtNode):
        # FDT_BEGIN_NODE
        struct_data.extend(struct.pack(">I", FDT_BEGIN_NODE))
        # Node name (null-terminated, 4-byte aligned)
        name_bytes = node.name.encode("ascii") + b"\x00"
        struct_data.extend(name_bytes)
        # Pad to 4-byte alignment
        pad = _align4(len(name_bytes)) - len(name_bytes)
        struct_data.extend(b"\x00" * pad)

        # Properties
        for prop in node.properties:
            struct_data.extend(struct.pack(">I", FDT_PROP))
            struct_data.extend(
                struct.pack(">II", len(prop.value), strings_map[prop.name])
            )
            struct_data.extend(prop.value)
            # Pad value to 4-byte alignment
            pad = _align4(len(prop.value)) - len(prop.value)
            struct_data.extend(b"\x00" * pad)

        # Children
        for child in node.children:
            emit_node(child)

        # FDT_END_NODE
        struct_data.extend(struct.pack(">I", FDT_END_NODE))

    emit_node(root)
    struct_data.extend(struct.pack(">I", FDT_END))

    # Memory reservation block (empty -- 16 bytes of zeros)
    mem_rsv = b"\x00" * 16

    # Header is 40 bytes
    header_size = 40
    off_mem_rsvmap = header_size
    off_dt_struct = off_mem_rsvmap + len(mem_rsv)
    off_dt_strings = off_dt_struct + len(struct_data)
    totalsize = off_dt_strings + len(strings_data)

    header = struct.pack(
        ">IIIIIIIIII",
        FDT_MAGIC,       # magic
        totalsize,       # totalsize
        off_dt_struct,   # off_dt_struct
        off_dt_strings,  # off_dt_strings
        off_mem_rsvmap,  # off_mem_rsvmap
        FDT_VERSION,     # version
        FDT_LAST_COMP_VERSION,  # last_comp_version
        0,               # boot_cpuid_phys
        len(strings_data),  # size_dt_strings
        len(struct_data),   # size_dt_struct
    )

    return bytes(header + mem_rsv + struct_data + strings_data)
