"""Shared fixtures for ipcgen embedded emitter tests."""

import pytest
import sys
import os

# Add the project root to sys.path so 'tools.ipcgen' is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from tools.ipcgen.lexer import tokenize
from tools.ipcgen.parser import Parser


ECHO_IDL = """\
service Echo
{
    [method=1]
    int Ping([in] uint32 value, [out] uint32 result);

    [method=2]
    int GetName([out] string[32] name);

    [method=3]
    int Add([in] uint32 a, [in] uint32 b, [out] uint32 sum);
};

notifications Echo
{
    [notify=1]
    void ValueChanged([in] uint32 newValue);

    [notify=2]
    void Heartbeat();
};
"""


TYPED_IDL = """\
enum DeviceType
{
    Sensor = 0,
    Actuator = 1,
    Controller = 2
};

struct DeviceInfo
{
    uint32 id;
    DeviceType type;
    uint8[6] serial;
};

service DeviceManager
{
    [method=1]
    int GetDeviceCount([out] uint32 count);

    [method=2]
    int GetDeviceInfo([in] uint32 deviceId, [out] DeviceInfo info);
};
"""


@pytest.fixture
def echo_idl():
    """Parsed Echo IDL."""
    tokens = tokenize(ECHO_IDL)
    return Parser(tokens).parse()


@pytest.fixture
def typed_idl():
    """Parsed DeviceManager IDL with enums and structs."""
    tokens = tokenize(TYPED_IDL)
    return Parser(tokens).parse()
