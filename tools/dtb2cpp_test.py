"""Tests for dtb2cpp.py -- DTB binary to C++ converter."""

import os
import struct
import sys
import tempfile

import pytest

# Add tools/ to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dtb2cpp import dtb_to_cpp, FDT_MAGIC
from fdtlib import FdtNode, FdtProperty, build_dtb


# -- Helpers --

def make_minimal_dtb() -> bytes:
    """Create a minimal valid DTB with just a root node."""
    root = FdtNode("")
    root.add_property(FdtProperty.from_string("compatible", "test"))
    return build_dtb(root)


def make_f407_dtb() -> bytes:
    """Create a DTB matching the STM32F407 board."""
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
    root.add_child(clocks)

    features = FdtNode("features")
    features.add_property(FdtProperty.from_bool("fpu"))
    root.add_child(features)

    return build_dtb(root)


# -- DTB validity tests --

class TestFdtlib:
    def test_magic_number(self):
        dtb = make_minimal_dtb()
        magic = struct.unpack(">I", dtb[:4])[0]
        assert magic == FDT_MAGIC

    def test_header_size(self):
        dtb = make_minimal_dtb()
        assert len(dtb) >= 40

    def test_totalsize_matches(self):
        dtb = make_minimal_dtb()
        totalsize = struct.unpack(">I", dtb[4:8])[0]
        assert totalsize == len(dtb)

    def test_version(self):
        dtb = make_minimal_dtb()
        version = struct.unpack(">I", dtb[20:24])[0]
        assert version == 17

    def test_f407_dtb_valid(self):
        dtb = make_f407_dtb()
        magic = struct.unpack(">I", dtb[:4])[0]
        assert magic == FDT_MAGIC

    def test_string_property_in_blob(self):
        dtb = make_minimal_dtb()
        # "compatible" and "test" should appear in the blob
        assert b"compatible" in dtb
        assert b"test" in dtb

    def test_u32_property_big_endian(self):
        root = FdtNode("")
        root.add_property(FdtProperty.from_u32("value", 0x12345678))
        dtb = build_dtb(root)
        # The value should appear in big-endian
        assert b"\x12\x34\x56\x78" in dtb

    def test_bool_property_zero_length(self):
        root = FdtNode("")
        root.add_property(FdtProperty.from_bool("flag"))
        dtb = build_dtb(root)
        assert b"flag" in dtb

    def test_reg_property_pair(self):
        root = FdtNode("")
        root.add_property(FdtProperty.from_u32_pair("reg", 0x08000000, 0x100000))
        dtb = build_dtb(root)
        assert b"\x08\x00\x00\x00\x00\x10\x00\x00" in dtb

    def test_nested_nodes(self):
        root = FdtNode("")
        child = FdtNode("child")
        grandchild = FdtNode("grandchild")
        grandchild.add_property(FdtProperty.from_string("key", "val"))
        child.add_child(grandchild)
        root.add_child(child)
        dtb = build_dtb(root)
        assert b"child" in dtb
        assert b"grandchild" in dtb
        assert b"key" in dtb


# -- dtb_to_cpp tests --

class TestDtb2Cpp:
    def test_output_contains_array_declaration(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        assert "g_boardDtb[]" in cpp
        assert "extern" in cpp

    def test_output_contains_size_variable(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        assert "g_boardDtbSize" in cpp
        assert "sizeof(g_boardDtb)" in cpp

    def test_output_contains_include(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        assert "#include <cstdint>" in cpp

    def test_output_contains_hex_bytes(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        # First byte of magic: 0xd0
        assert "0xd0" in cpp

    def test_source_path_in_comment(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb, "board.dtb")
        assert "board.dtb" in cpp
        assert "DO NOT EDIT" in cpp

    def test_no_source_path(self):
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        assert "Auto-generated" in cpp

    def test_bad_magic_raises(self):
        bad = b"\x00\x00\x00\x00" + b"\x00" * 36
        with pytest.raises(ValueError, match="Bad DTB magic"):
            dtb_to_cpp(bad)

    def test_too_small_raises(self):
        with pytest.raises(ValueError, match="too small"):
            dtb_to_cpp(b"\xd0\x0d")

    def test_round_trip_bytes_preserved(self):
        """Verify all DTB bytes appear in the C++ output."""
        dtb = make_minimal_dtb()
        cpp = dtb_to_cpp(dtb)
        for byte in dtb:
            assert f"0x{byte:02x}" in cpp

    def test_f407_round_trip(self):
        dtb = make_f407_dtb()
        cpp = dtb_to_cpp(dtb)
        assert "g_boardDtb[]" in cpp
        assert "g_boardDtbSize" in cpp
        # Verify magic bytes present
        assert "0xd0, 0x0d, 0xfe, 0xed" in cpp


# -- File I/O tests --

class TestDtb2CppCli:
    def test_file_write(self):
        dtb = make_minimal_dtb()
        with tempfile.NamedTemporaryFile(suffix=".dtb", delete=False) as f:
            f.write(dtb)
            dtb_path = f.name

        try:
            cpp_path = dtb_path.replace(".dtb", ".cpp")
            from dtb2cpp import main as dtb2cpp_main

            old_argv = sys.argv
            sys.argv = ["dtb2cpp", dtb_path, "-o", cpp_path]
            try:
                dtb2cpp_main()
            finally:
                sys.argv = old_argv

            with open(cpp_path) as f:
                content = f.read()
            assert "g_boardDtb[]" in content
            os.unlink(cpp_path)
        finally:
            os.unlink(dtb_path)
