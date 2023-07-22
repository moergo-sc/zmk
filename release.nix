{ pkgs ? (import ./nix/pinned-nixpkgs.nix {}), revision ? "HEAD" }:

let
  lib = pkgs.lib;
  zmkPkgs = (import ./default.nix { inherit pkgs; });
  lambda  = (import ./lambda { inherit pkgs; });
  ccacheWrapper = pkgs.callPackage ./nix/ccache.nix {};

  nix-utils = pkgs.fetchFromGitHub {
    owner = "iknow";
    repo = "nix-utils";
    rev = "c13c7a23836c8705452f051d19fc4dff05533b53";
    sha256 = "0ax7hld5jf132ksdasp80z34dlv75ir0ringzjs15mimrkw8zcac";
  };

  ociTools = pkgs.callPackage "${nix-utils}/oci" {};

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

  zmkCompileScript = let
    zmk' = zmk.override {
      gcc-arm-embedded = ccacheWrapper.override {
        unwrappedCC = pkgs.gcc-arm-embedded;
      };
    };
    zmk_glove80_rh = zmk.override { board = "glove80_rh"; };
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

    keymap="${zmk.src}/app/boards/arm/glove80/glove80.keymap"
    kconfig=""
    board="glove80_lh"
    merge_rhs=""

    while getopts "hk:c:d:b:m" opt; do
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
        m)
          merge_rhs=t
          ;;
      esac
    done

    if [ "$board" = "glove80_rh" -a -n "$merge_rhs" ]; then
      echo "Cannot merge static RHS with built RHS" >&2
      exit 2
    fi

    keymap="$(checkPath "$keymap" keymap)"
    kconfig="$(checkPath "$kconfig" Kconfig)"

    export PATH=${lib.makeBinPath (with pkgs; zmk'.nativeBuildInputs ++ [ ccache ])}:$PATH
    export CMAKE_PREFIX_PATH=${zephyr}

    export CCACHE_BASEDIR=$PWD
    export CCACHE_NOHASHDIR=t
    export CCACHE_COMPILERCHECK=none

    if [ -n "$DEBUG" ]; then ccache -z; fi

    cmake -G Ninja -S ${zmk'.src}/app ${lib.escapeShellArgs zmk'.cmakeFlags} "-DUSER_CACHE_DIR=/tmp/.cache" "-DKEYMAP_FILE=$keymap" "-DCONF_FILE=$kconfig" "-DBOARD=$board" "-DBUILD_VERSION=${revision}"

    ninja

    if [ -n "$DEBUG" ]; then ccache -s; fi

    if [ -n "$merge_rhs" ]; then
      cat zephyr/zmk.uf2 ${zmk_glove80_rh}/zmk.uf2 > zmk.uf2
    else
      mv zephyr/zmk.uf2 zmk.uf2
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
    export PATH=${lib.makeBinPath [ zmkCompileScript ]}:$PATH
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
  inherit lambdaImage zmkCompileScript ccacheCache;
  directLambdaImage = lambdaImage;

  # nix shell -f release.nix simulateLambda -c simulateLambda
  inherit simulateLambda;
}
