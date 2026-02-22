"""Tests for the embedded (kernel IPC) code emitter."""

import pytest
from tools.ipcgen.embedded_emitter import (
    emit_types_h,
    emit_server_h,
    emit_server_cpp,
    emit_client_h,
    emit_client_cpp,
)
from tools.ipcgen.types import fnv1a_32


# -- Types header -----------------------------------------------------------

class TestTypesHeader:
    def test_pragma_once(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "#pragma once" in code

    def test_kernel_ipc_namespace(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "namespace kernel" in code
        assert "namespace ipc" in code

    def test_cstdint_include(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "#include <cstdint>" in code

    def test_no_vector_include(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "<vector>" not in code

    def test_enum_values(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "Sensor = 0," in code
        assert "Actuator = 1," in code
        assert "Controller = 2," in code

    def test_struct_fields(self, typed_idl):
        code = emit_types_h(typed_idl)
        assert "uint32_t id;" in code
        assert "DeviceType type;" in code
        assert "uint8_t serial[6];" in code

    def test_no_std_array(self, typed_idl):
        """Embedded types use C arrays, not std::array."""
        code = emit_types_h(typed_idl)
        assert "std::array" not in code


# -- Server header ----------------------------------------------------------

class TestServerHeader:
    def test_class_name(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "class EchoServer" in code

    def test_service_id(self, echo_idl):
        code = emit_server_h(echo_idl)
        sid = fnv1a_32("Echo")
        assert f"0x{sid:08x}u" in code

    def test_method_enum(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "kPing = 1," in code
        assert "kGetName = 2," in code
        assert "kAdd = 3," in code

    def test_notify_enum(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "kValueChanged = 1," in code
        assert "kHeartbeat = 2," in code

    def test_run_method(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "void run();" in code

    def test_pure_virtual_handlers(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "virtual std::int32_t handlePing(" in code
        assert "= 0;" in code

    def test_handler_params(self, echo_idl):
        code = emit_server_h(echo_idl)
        # handlePing: [in] uint32 value, [out] uint32 *result
        assert "uint32_t value" in code
        assert "uint32_t *result" in code

    def test_includes_kernel_ipc(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert '#include "kernel/Ipc.h"' in code

    def test_no_vector_include(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "<vector>" not in code

    def test_notification_sender(self, echo_idl):
        code = emit_server_h(echo_idl)
        assert "notifyValueChanged" in code
        assert "notifyHeartbeat" in code


# -- Server implementation -------------------------------------------------

class TestServerCpp:
    def test_run_calls_receive(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "kernel::messageReceive(&msg)" in code

    def test_dispatch_switch(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "switch (request.methodId)" in code
        assert "case kPing:" in code
        assert "case kGetName:" in code
        assert "case kAdd:" in code

    def test_calls_message_reply(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "kernel::messageReply(request.sender, reply)" in code

    def test_payload_static_assert(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "static_assert" in code
        assert "kernel::kMaxPayloadSize" in code

    def test_default_case_error(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "kernel::kIpcErrMethod" in code

    def test_handler_call(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "handlePing(" in code
        assert "handleGetName(" in code
        assert "handleAdd(" in code

    def test_memcpy_marshaling(self, echo_idl):
        code = emit_server_cpp(echo_idl)
        assert "std::memcpy" in code


# -- Client header ----------------------------------------------------------

class TestClientHeader:
    def test_class_name(self, echo_idl):
        code = emit_client_h(echo_idl)
        assert "class EchoClient" in code

    def test_constructor(self, echo_idl):
        code = emit_client_h(echo_idl)
        assert "explicit EchoClient(kernel::ThreadId serverTid)" in code
        assert "m_serverTid(serverTid)" in code

    def test_method_stubs(self, echo_idl):
        code = emit_client_h(echo_idl)
        assert "std::int32_t Ping(" in code
        assert "std::int32_t GetName(" in code
        assert "std::int32_t Add(" in code

    def test_private_server_tid(self, echo_idl):
        code = emit_client_h(echo_idl)
        assert "kernel::ThreadId m_serverTid;" in code

    def test_no_vector_include(self, echo_idl):
        code = emit_client_h(echo_idl)
        assert "<vector>" not in code


# -- Client implementation -------------------------------------------------

class TestClientCpp:
    def test_calls_message_send(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "kernel::messageSend(m_serverTid, request, &reply)" in code

    def test_returns_reply_status(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "return reply.status;" in code

    def test_transport_error_check(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "if (rc != kernel::kIpcOk)" in code
        assert "return rc;" in code

    def test_payload_static_assert(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "static_assert" in code
        assert "kernel::kMaxPayloadSize" in code

    def test_memcpy_marshaling(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "std::memcpy" in code

    def test_request_type_set(self, echo_idl):
        code = emit_client_cpp(echo_idl)
        assert "kernel::MessageType::Request" in code


# -- Types-with-enums server -----------------------------------------------

class TestTypedServer:
    def test_includes_types_header(self, typed_idl):
        code = emit_server_h(typed_idl)
        assert '#include "DeviceManagerTypes.h"' in code

    def test_handler_uses_user_types(self, typed_idl):
        code = emit_server_h(typed_idl)
        assert "DeviceInfo *info" in code


class TestTypedClient:
    def test_includes_types_header(self, typed_idl):
        code = emit_client_h(typed_idl)
        assert '#include "DeviceManagerTypes.h"' in code


# -- End-to-end: generate all files for Echo --------------------------------

class TestEndToEnd:
    def test_all_files_non_empty(self, echo_idl):
        types_h = emit_types_h(echo_idl)
        server_h = emit_server_h(echo_idl)
        server_cpp = emit_server_cpp(echo_idl)
        client_h = emit_client_h(echo_idl)
        client_cpp = emit_client_cpp(echo_idl)

        # Types header is empty for Echo (no enums/structs) -- that's OK,
        # the CLI skips it. But the function returns valid code.
        for code in [server_h, server_cpp, client_h, client_cpp]:
            assert len(code) > 100, "Generated code seems too short"

    def test_auto_generated_comment(self, echo_idl):
        for func in [emit_server_h, emit_server_cpp, emit_client_h, emit_client_cpp]:
            code = func(echo_idl)
            assert "Auto-generated by ipcgen (embedded)" in code
