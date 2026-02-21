{
  description = "ms-os: Microkernel RTOS for ARM Cortex-M";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          name = "ms-os";

          packages = with pkgs; [
            gcc-arm-embedded
            cmake
            ninja
            openocd
            gdb
            (python3.withPackages (ps: [ ps.pyserial ps.pytest ]))
            git
            stlink
            clang-tools
          ];

          shellHook = ''
            export PS1="[ms-os] \w $ "
          '';
        };
      }
    );
}
