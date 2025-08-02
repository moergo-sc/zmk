{ pkgs ? (import ./nix/pinned-nixpkgs.nix {})
, revision ? "HEAD"
, firmwareVersion ? revision
 }:

let
  lib = pkgs.lib;
  zmkPkgs = (import ./default.nix { inherit pkgs; });
  lambda  = (import ./lambda { inherit pkgs; });
  ccacheWrapper = pkgs.callPackage ./nix/ccache.nix {};

  nix-utils = pkgs.fetchFromGitHub {
    owner = "iknow";
    repo = "nix-utils";
    rev = "0dd7ce551c426af66eac7eb1bb750f94cb6986c3";
    sha256 = "sha256-UdtvdIssn4u9M7qUBd/6zDjPcF5gaYTUjyqMny0LprY=";
  };

  ociTools = pkgs.callPackage "${nix-utils}/oci" {};

  semver = pkgs.callPackage ./nix/semver.nix {};

  inherit (zmkPkgs) zmk zephyr;

  accounts = {
    users.deploy = {
      uid = 999;
      group = "deploy";
      home = "/home/deploy";
      shell = "/bin/sh";
    };
    groups.deploy.gid = 999;
  };

  baseLayer = {
    name = "base-layer";
    path = [ pkgs.busybox ];
    entries = ociTools.makeFilesystem {
      inherit accounts;
      tmp = true;
      usrBinEnv = "${pkgs.busybox}/bin/env";
      binSh = "${pkgs.busybox}/bin/sh";
    };
  };

  depsLayer = {
    name = "deps-layer";
    path = [ pkgs.ccache ];
    includes = zmk.buildInputs ++ zmk.nativeBuildInputs ++ zmk.zephyrModuleDeps;
  };

  dts2yml = pkgs.writeShellScriptBin "dts2yml" ''
    set -eo pipefail

    ${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-cpp -P -D__DTS__ -E -nostdinc \
      -I "${zmk.src}/app/dts" -I "${zmk.src}/app/include" \
      -I "${zephyr}/zephyr/dts" -I "${zephyr}/zephyr/dts/common" -I "${zephyr}/zephyr/dts/arm" \
      -I "${zephyr}/zephyr/include" -I "${zephyr}/zephyr/include/zephyr"\
      -undef -x assembler-with-cpp - |\
    ${pkgs.dtc}/bin/dtc -I dts -O yaml
  '';

  zmkCompileScript = let
    zmk' = zmk.override {
      gcc-arm-embedded = ccacheWrapper.override {
        unwrappedCC = pkgs.gcc-arm-embedded;
      };
    };
    zmk_glove80_rh = zmk.override { board = "glove80_rh"; };
    zmk_go60_rh = zmk.override { board = "go60_rh"; };
    realpath_coreutils = if pkgs.stdenv.isDarwin then pkgs.coreutils else pkgs.busybox;
  in pkgs.writeShellScriptBin "compileZmk" ''
    set -eo pipefail

    function usage() {
      echo "Usage: compileZmk [-m] [-k keymap_file] [-c kconfig_file] [-b board]"
    }

    function checkPath() {
      if [ -z "$1" ]; then
        return 0
      elif [ ! -f "$1" ]; then
        echo "Error: Missing $2 file" >&2
        usage >&2
        exit 1
      fi

      ${realpath_coreutils}/bin/realpath "$1"
    }

    keymap=""
    kconfig=""
    board="glove80_lh"
    merge_rhs=""
    snippet=""

    while getopts "hk:c:d:b:s:m" opt; do
      case "$opt" in
        h|\?)
          usage >&2
          exit 1
          ;;
        k)
          keymap="$OPTARG"
          ;;
        c)
          kconfig="$OPTARG"
          ;;
        b)
          board="$OPTARG"
          ;;
        s)
          snippet="$OPTARG"
          ;;
        m)
          merge_rhs=t
          ;;
      esac
    done

    if [ -n "$merge_rhs" ]; then
      case "$board" in
      glove80_lh)
        merge_rhs_firmware="${zmk_glove80_rh}/zmk.uf2"
        ;;
      go60_lh)
        merge_rhs_firmware="${zmk_go60_rh}/zmk.uf2"
        ;;
      *)
        echo "No pre-built RHS exists to merge with specified board '$board'" >&2
        exit 2
        ;;
      esac
    fi

    keymap="$(checkPath "$keymap" keymap)"
    kconfig="$(checkPath "$kconfig" Kconfig)"

    export PATH=${lib.makeBinPath (with pkgs; zmk'.nativeBuildInputs ++ [ ccache ])}:$PATH
    export CMAKE_PREFIX_PATH=${zephyr}

    export CCACHE_BASEDIR=$PWD
    export CCACHE_NOHASHDIR=t
    export CCACHE_COMPILERCHECK=none

    if [ -n "$DEBUG" ]; then ccache -z; fi

    declare -a cmakeExtraFlags

    if [ -n "$keymap" ]; then
      cmakeExtraFlags+=("-DKEYMAP_FILE=$keymap")
    fi

    if [ -n "$kconfig" ]; then
      cmakeExtraFlags+=("-DEXTRA_CONF_FILE=$kconfig")
    fi

    if [ -n "$snippet" ]; then
      cmakeExtraFlags+=("-DSNIPPET=$snippet")
    fi

    if ${semver}/bin/semver validate "${firmwareVersion}"; then
      firmwareMajor="$(${semver}/bin/semver format "{{.Major}}" "${firmwareVersion}")"
      firmwareMinor="$(${semver}/bin/semver format "{{.Minor}}" "${firmwareVersion}")"
      firmwarePatch="$(${semver}/bin/semver format "{{.Patch}}" "${firmwareVersion}")"
    else
      firmwareMajor="0"
      firmwareMinor="0"
      firmwarePatch="0"
    fi

    firmwareFlags="-DMOERGO_FIRMWARE_VERSION=${firmwareVersion};-DMOERGO_FIRMWARE_VERSION_MAJOR=$firmwareMajor;-DMOERGO_FIRMWARE_VERSION_MINOR=$firmwareMinor;-DMOERGO_FIRMWARE_VERSION_PATCH=$firmwarePatch"

    cmake -G Ninja -S ${zmk'.src}/app ${lib.escapeShellArgs zmk'.cmakeFlags} "-DUSER_CACHE_DIR=/tmp/.cache" "-DBOARD=$board" "-DBUILD_VERSION=${revision}" "-DDTS_EXTRA_CPPFLAGS=$firmwareFlags" "''${cmakeExtraFlags[@]}"

    ninja

    if [ -n "$DEBUG" ]; then ccache -s; fi

    if [ -z "$merge_rhs" ]; then
      mv zephyr/zmk.uf2 zmk.uf2
    else
      cat zephyr/zmk.uf2 "$merge_rhs_firmware" > zmk.uf2
    fi
  '';

  ccacheCache = pkgs.runCommandNoCC "ccache-cache" {
    nativeBuildInputs = [ zmkCompileScript ];
  } ''
    export CCACHE_DIR=$out

    mkdir /tmp/build
    cd /tmp/build

    compileZmk -b glove80_lh -k ${zmk.src}/app/boards/arm/glove80/glove80.keymap

    rm -fr /tmp/build
    mkdir /tmp/build
    cd /tmp/build

    compileZmk -b glove80_rh -k ${zmk.src}/app/boards/arm/glove80/glove80.keymap

    rm -fr /tmp/build
    mkdir /tmp/build
    cd /tmp/build

    compileZmk -b go60_lh -k ${zmk.src}/app/boards/arm/go60/go60.keymap

    rm -fr /tmp/build
    mkdir /tmp/build
    cd /tmp/build

    compileZmk -b go60_rh -k ${zmk.src}/app/boards/arm/go60/go60.keymap
  '';

  entrypoint = pkgs.writeShellScriptBin "entrypoint" ''
    set -euo pipefail

    if [ ! -d "$CCACHE_DIR" ]; then
      cp -r ${ccacheCache} "$CCACHE_DIR"
      chmod -R u=rwX,go=u-w "$CCACHE_DIR"
    fi

    if [ ! -d /tmp/build ]; then
      mkdir /tmp/build
    fi

    exec "$@"
  '';

  startLambda = pkgs.writeShellScriptBin "startLambda" ''
    set -euo pipefail
    export PATH=${lib.makeBinPath [ zmkCompileScript dts2yml ]}:$PATH
    cd ${lambda.source}
    ${lambda.bundleEnv}/bin/bundle exec aws_lambda_ric "app.LambdaFunction::Handler.process"
  '';

  simulateLambda = pkgs.writeShellScriptBin "simulateLambda" ''
    ${pkgs.aws-lambda-rie}/bin/aws-lambda-rie ${startLambda}/bin/startLambda
  '';

  lambdaImage =
  let
    appLayer = {
      name = "app-layer";
      path = [ startLambda zmkCompileScript ];
    };
  in
  ociTools.makeSimpleImage {
    name = "zmk-builder-lambda";
    layers = [ baseLayer depsLayer appLayer ];
    config = {
      User = "deploy";
      WorkingDir = "/tmp";
      Entrypoint = [ "${entrypoint}/bin/entrypoint" ];
      Cmd = [ "startLambda" ];
      Env = [ "CCACHE_DIR=/tmp/ccache" "REVISION=${revision}" ];
    };
  };
in {
  inherit lambdaImage zmkCompileScript dts2yml ccacheCache;
  directLambdaImage = lambdaImage;

  # nix shell -f release.nix simulateLambda -c simulateLambda
  inherit simulateLambda;
}
