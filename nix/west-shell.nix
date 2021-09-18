{ pkgs ? (import ./pinned-nixpkgs.nix {}) }:

let
  # from zephyr/scripts/requirements-base.txt
  pythonDependencies = ps: with ps; [
    pyelftools
    pyyaml
    packaging
    progress
    anytree
    intelhex
    west
  ];

  requiredStdenv =
    if pkgs.stdenv.hostPlatform.isLinux
    then pkgs.multiStdenv
    else pkgs.stdenv;
in
with pkgs;
# requires multiStdenv to build 32-bit test binaries
requiredStdenv.mkDerivation {
  name = "zmk-shell";

  buildInputs = [
    # ZMK dependencies
    gitFull
    wget
    autoconf
    automake
    bzip2
    ccache
    dtc # devicetree compiler
    dfu-util
    gcc
    libtool
    ninja
    cmake
    xz
    (python3.withPackages(pythonDependencies))

    # ARM toolchain
    gcc-arm-embedded
  ];

  ZEPHYR_TOOLCHAIN_VARIANT = "gnuarmemb";
  GNUARMEMB_TOOLCHAIN_PATH = gcc-arm-embedded;

  shellHook = "if [ ! -d \"zephyr\" ]; then west init -l app/ ; west update; west zephyr-export; fi; source zephyr/zephyr-env.sh";
}
