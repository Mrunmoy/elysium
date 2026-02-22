"""
CLI entry point for ipcgen (embedded backend).

Usage:
    python3 -m tools.ipcgen services/echo/Echo.idl --outdir gen/
"""

import argparse
import os

from .lexer import tokenize
from .parser import Parser
from .embedded_emitter import (
    emit_types_h,
    emit_server_h,
    emit_server_cpp,
    emit_client_h,
    emit_client_cpp,
)
from .types import fnv1a_32


def main():
    parser = argparse.ArgumentParser(description="ms-os IDL code generator (embedded)")
    parser.add_argument("idl", help="Input .idl file")
    parser.add_argument("--outdir", required=True, help="Output directory")
    args = parser.parse_args()

    with open(args.idl) as f:
        text = f.read()

    tokens = tokenize(text)
    idl = Parser(tokens).parse()

    name = idl.service_name
    os.makedirs(args.outdir, exist_ok=True)

    files = []

    if idl.enums or idl.structs:
        files.append((f"{name}Types.h", emit_types_h(idl)))

    files.extend([
        (f"{name}Server.h",   emit_server_h(idl)),
        (f"{name}Server.cpp", emit_server_cpp(idl)),
        (f"{name}Client.h",   emit_client_h(idl)),
        (f"{name}Client.cpp", emit_client_cpp(idl)),
    ])

    for filename, content in files:
        path = os.path.join(args.outdir, filename)
        with open(path, "w") as f:
            f.write(content)
        print(f"  wrote {path}")

    print(f"\nGenerated {len(files)} files for service '{name}' "
          f"(serviceId=0x{fnv1a_32(name):08x})")


if __name__ == "__main__":
    main()
