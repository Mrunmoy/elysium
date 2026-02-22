"""
CLI entry point for dtgen (device tree code generator).

Usage:
    python3 -m tools.dtgen boards/stm32f407zgt6.yaml --outdir boards/stm32f407zgt6/
"""

import argparse
import os

from .schema import parse_board_yaml
from .emitter import emit_board_config_h


def main():
    parser = argparse.ArgumentParser(
        description="ms-os device tree code generator"
    )
    parser.add_argument("yaml", help="Input board .yaml file")
    parser.add_argument("--outdir", required=True, help="Output directory")
    args = parser.parse_args()

    with open(args.yaml) as f:
        yaml_str = f.read()

    bd = parse_board_yaml(yaml_str)

    os.makedirs(args.outdir, exist_ok=True)

    filename = "BoardConfig.h"
    code = emit_board_config_h(bd, source_path=args.yaml)
    path = os.path.join(args.outdir, filename)

    with open(path, "w") as f:
        f.write(code)

    print(f"  wrote {path}")
    print(f"\nGenerated BoardConfig.h for board '{bd.board_name}'")


if __name__ == "__main__":
    main()
