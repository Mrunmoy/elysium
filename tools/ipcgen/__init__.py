"""
ipcgen: IDL code generator for ms-os kernel IPC.

Reuses lexer, parser, and types from ms-ipc.
Adds an embedded emitter that generates C++ code targeting the kernel
message-passing API (messageSend/Receive/Reply).
"""
