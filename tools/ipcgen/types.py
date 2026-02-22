"""
Type system: IDL-to-C++ type mapping and FNV-1a hash for service IDs.
"""

# IDL type name → C++ type name.
TYPE_MAP = {
    "uint8":   "uint8_t",
    "uint16":  "uint16_t",
    "uint32":  "uint32_t",
    "uint64":  "uint64_t",
    "int8":    "int8_t",
    "int16":   "int16_t",
    "int32":   "int32_t",
    "int64":   "int64_t",
    "float32": "float",
    "float64": "double",
    "bool":    "bool",
    "string":  "char",
}


def cpp_type(idl_type: str) -> str:
    """Map an IDL type name to its C++ equivalent."""
    return TYPE_MAP[idl_type]


def fnv1a_32(s: str) -> int:
    """FNV-1a 32-bit hash. Used to derive serviceId from the service name."""
    h = 0x811c9dc5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h
